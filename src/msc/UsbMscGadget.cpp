#include "UsbMscGadget.h"
#include "DemoFatImage.h"
#include "USB.h"
#include "USBMSC.h"
#include <cstring>

#if !ARDUINO_USB_MODE

static UsbMscGadget* g_msc = nullptr;
static USBMSC MSC;

static bool isDirLba(uint32_t lba) {
    return lba >= 35 && lba <= 42;
}

static int32_t pidrive_msc_read(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize) {
    if (!g_msc) return 0;
    return g_msc->onRead(lba, offset, buffer, bufsize);
}

static int32_t pidrive_msc_write(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize) {
    if (!g_msc) return 0;
    return g_msc->onWrite(lba, offset, buffer, bufsize);
}

static bool pidrive_msc_start_stop(uint8_t power_condition, bool start, bool load_eject) {
    (void)power_condition;
    if (g_msc) g_msc->onHostStartStop(start, load_eject);
    return true;
}

static void usb_event_cb(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    (void)arg;
    (void)event_data;
    if (event_base != ARDUINO_USB_EVENTS || !g_msc) return;
    switch (event_id) {
        case ARDUINO_USB_STARTED_EVENT: g_msc->onUsbPlugged(true); break;
        case ARDUINO_USB_STOPPED_EVENT: g_msc->onUsbPlugged(false); break;
        case ARDUINO_USB_SUSPEND_EVENT: g_msc->onUsbSuspend(true); break;
        case ARDUINO_USB_RESUME_EVENT: g_msc->onUsbSuspend(false); break;
        default: break;
    }
}

void UsbMscGadget::loadDefaultSlots() {
    // Geometry from demo_fat.bin
    const char* names[] = {"ROCK FM", "Antenne 1", "SWR3", "About"};
    const char* uids[] = {"demo:rock_fm", "demo:antenne", "demo:swr3", "action:about"};
    const char* paths[] = {
        "STATIONS/01ROCK.MP3", "STATIONS/02ANTENN.MP3", "STATIONS/03SWR3.MP3", "SETTINGS/ABOUT.MP3"};
    const uint32_t ranges[][2] = {{43, 58}, {59, 74}, {75, 90}, {91, 102}};
    for (size_t i = 0; i < kSlots; i++) {
        slots_[i].lbaStart = ranges[i][0];
        slots_[i].lbaEnd = ranges[i][1];
        strncpy(slots_[i].name, names[i], sizeof(slots_[i].name) - 1);
        strncpy(slots_[i].uid, uids[i], sizeof(slots_[i].uid) - 1);
        strncpy(slots_[i].path, paths[i], sizeof(slots_[i].path) - 1);
        slots_[i].active = true;
    }
}

void UsbMscGadget::applyMenuSlots(const MenuStore& menu) {
    loadDefaultSlots();
    size_t n = menu.count();
    if (n > kSlots) n = kSlots;
    for (size_t i = 0; i < kSlots; i++) {
        if (i < n) {
            const MenuItem* it = menu.itemAt(i);
            if (!it) continue;
            strncpy(slots_[i].uid, it->uid, sizeof(slots_[i].uid) - 1);
            strncpy(slots_[i].name, it->name, sizeof(slots_[i].name) - 1);
            slots_[i].active = true;
        } else {
            slots_[i].active = false;
            slots_[i].uid[0] = 0;
        }
    }
    // re-bind stream slot if still active
    if (stream_ && stream_->active() && stream_->uid()[0]) {
        startStream(stream_->uid());
    }
    if (events_) {
        char d[32];
        snprintf(d, sizeof(d), "slots=%u", (unsigned)n);
        events_->push("msc.slots", d);
    }
    Serial.printf("[MSC] applyMenuSlots n=%u\n", (unsigned)n);
}

void UsbMscGadget::startStream(const char* uid) {
    streamSlot_ = -1;
    streamLba0_ = streamLba1_ = 0;
    streamStartCl_ = streamEndCl_ = 0;
    if (!uid || !uid[0]) return;
    for (size_t i = 0; i < kSlots; i++) {
        if (slots_[i].active && strcmp(slots_[i].uid, uid) == 0) {
            streamSlot_ = (int)i;
            streamLba0_ = slots_[i].lbaStart;
            streamLba1_ = kStreamLbaEnd;
            if (streamLba1_ >= DEMO_FAT_SECTOR_COUNT) streamLba1_ = DEMO_FAT_SECTOR_COUNT - 1;
            streamStartCl_ = lbaToCluster(streamLba0_);
            streamEndCl_ = lbaToCluster(streamLba1_);
            slots_[i].lbaEnd = streamLba1_;
            Serial.printf("[MSC] stream slot=%d lba=%u..%u cl=%u..%u\n", streamSlot_,
                          (unsigned)streamLba0_, (unsigned)streamLba1_, (unsigned)streamStartCl_,
                          (unsigned)streamEndCl_);
            if (events_) events_->push("msc.stream_on", uid);
            return;
        }
    }
    Serial.printf("[MSC] stream uid not in slots: %s\n", uid);
}

void UsbMscGadget::stopStream() {
    if (streamSlot_ >= 0) {
        loadDefaultSlots();  // restore geometry; names re-applied via next menu_set
        if (menu_) applyMenuSlots(*menu_);
    }
    streamSlot_ = -1;
    streamLba0_ = streamLba1_ = 0;
    streamStartCl_ = streamEndCl_ = 0;
    if (events_) events_->push("msc.stream_off", "");
}

static void fat12Set(uint8_t* fat, uint16_t cl, uint16_t val) {
    // fat points at start of FAT region in a linear buffer — we patch within one sector view
    // Caller passes pointer to full FAT image slice; for sector-local we need absolute index.
    size_t i = cl + (cl / 2);
    if (cl & 1) {
        fat[i] = (uint8_t)((fat[i] & 0x0F) | ((val & 0x0F) << 4));
        fat[i + 1] = (uint8_t)(val >> 4);
    } else {
        fat[i] = (uint8_t)(val & 0xFF);
        fat[i + 1] = (uint8_t)((fat[i + 1] & 0xF0) | ((val >> 8) & 0x0F));
    }
}

void UsbMscGadget::patchFatForStream(uint8_t* sector, uint32_t lba) const {
    if (streamSlot_ < 0 || streamStartCl_ < 2) return;
    // Demo FAT: reserved=1, fats=2, fatz=2 → FAT0 at LBA 1-2, FAT1 at LBA 3-4
    if (lba != 1 && lba != 2 && lba != 3 && lba != 4) return;

    // Build a tiny absolute FAT view for clusters we touch by reading both FAT sectors from image
    // into stack is heavy; instead patch only entries whose bytes fall in this sector.
    uint32_t fatIndex = (lba == 1 || lba == 3) ? 0 : 1;  // which sector of a FAT copy
    uint32_t fatBase = fatIndex * 512;

    for (uint16_t cl = streamStartCl_; cl <= streamEndCl_; cl++) {
        uint16_t next = (cl < streamEndCl_) ? (uint16_t)(cl + 1) : 0xFFF;
        size_t byteIndex = cl + (cl / 2);  // first byte of entry in FAT
        // entry spans byteIndex and byteIndex+1
        if (byteIndex >= fatBase + 512 && byteIndex + 1 < fatBase) continue;
        // Work on a 2-sector window around this FAT half — patch into `sector` when bytes map here
        uint8_t tmp[4];
        // load neighboring bytes from DEMO image for correct nibble merge
        uint32_t abs0 = 512u + byteIndex;  // FAT0 starts at LBA1 = offset 512
        if (lba == 3 || lba == 4) abs0 = 512u * 3 + byteIndex;  // FAT1
        // Actually: LBA1 offset=512, LBA2 offset=1024 for FAT0
        uint32_t fatStartOff = (lba <= 2) ? 512u : 1536u;
        (void)fatStartOff;
        // Simpler: copy from DEMO into local 1024 buffer once per call — too heavy in loop.
        // Patch only if both bytes of entry lie in this sector:
        size_t local = byteIndex - fatBase;
        if (local < 511) {
            uint8_t a = sector[local];
            uint8_t b = sector[local + 1];
            uint8_t pair[2] = {a, b};
            if (cl & 1) {
                pair[0] = (uint8_t)((pair[0] & 0x0F) | ((next & 0x0F) << 4));
                pair[1] = (uint8_t)(next >> 4);
            } else {
                pair[0] = (uint8_t)(next & 0xFF);
                pair[1] = (uint8_t)((pair[1] & 0xF0) | ((next >> 8) & 0x0F));
            }
            sector[local] = pair[0];
            sector[local + 1] = pair[1];
        } else if (local == 511) {
            // spans sector boundary — patch low byte here; high on next sector read
            if (cl & 1) {
                sector[511] = (uint8_t)((sector[511] & 0x0F) | ((next & 0x0F) << 4));
            } else {
                sector[511] = (uint8_t)(next & 0xFF);
            }
        }
    }
    (void)fat12Set;
}

void UsbMscGadget::patchDirForStream(uint8_t* sector, uint32_t lba) const {
    if (streamSlot_ < 0 || streamSlot_ >= (int)kSlots) return;
    // STATIONS directory lives at cluster 2 → LBA 35 (+ maybe 36-38)
    if (lba < 35 || lba > 38) return;
    // 8.3 names in demo: 01ROCK  02ANTENN 03SWR3 — slot index → order
    const char* shortNames[3] = {"01ROCK  ", "02ANTENN", "03SWR3  "};
    if (streamSlot_ > 2) return;  // ABOUT in SETTINGS — skip expand for now
    const char* want = shortNames[streamSlot_];
    uint32_t fileBytes = (streamLba1_ - streamLba0_ + 1) * 512u;
    for (int i = 0; i < 512; i += 32) {
        if (sector[i] == 0x00) break;
        if (sector[i] == 0xE5 || sector[i + 11] == 0x0F) continue;
        if (sector[i + 11] & 0x08) continue;
        if (sector[i + 11] & 0x10) continue;
        if (memcmp(sector + i, want, 8) != 0) continue;
        // start cluster
        sector[i + 26] = (uint8_t)(streamStartCl_ & 0xFF);
        sector[i + 27] = (uint8_t)(streamStartCl_ >> 8);
        sector[i + 28] = (uint8_t)(fileBytes & 0xFF);
        sector[i + 29] = (uint8_t)((fileBytes >> 8) & 0xFF);
        sector[i + 30] = (uint8_t)((fileBytes >> 16) & 0xFF);
        sector[i + 31] = (uint8_t)((fileBytes >> 24) & 0xFF);
        break;
    }
}

int32_t UsbMscGadget::onRead(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize) {
    if (lba >= DEMO_FAT_SECTOR_COUNT) return -1;
    uint32_t pos = lba * DEMO_FAT_SECTOR_SIZE + offset;
    if (pos >= DEMO_FAT_SIZE) return -1;
    uint32_t avail = DEMO_FAT_SIZE - pos;
    if (bufsize > avail) bufsize = avail;
    memcpy(buffer, DEMO_FAT_IMAGE + pos, bufsize);

    uint8_t* out = (uint8_t*)buffer;
    // Apply FAT/DIR patches when streaming (sector-aligned reads expected)
    if (streamSlot_ >= 0 && offset == 0 && bufsize >= 512) {
        for (uint32_t off = 0; off + 512 <= bufsize; off += 512) {
            uint32_t sec = lba + off / 512;
            patchFatForStream(out + off, sec);
            patchDirForStream(out + off, sec);
        }
    } else if (streamSlot_ >= 0 && offset == 0 && bufsize == 512) {
        patchFatForStream(out, lba);
        patchDirForStream(out, lba);
    }

    // Live MP3 payload for expanded stream LBAs
    if (stream_ && stream_->active() && streamSlot_ >= 0 && lba >= streamLba0_ && lba <= streamLba1_) {
        uint32_t fileOff = (lba - streamLba0_) * DEMO_FAT_SECTOR_SIZE + offset;
        stream_->readAt(fileOff, out, bufsize);
        streamBytesServed_ += bufsize;
    }

    noteDataRead(lba, bufsize);
    return (int32_t)bufsize;
}


bool UsbMscGadget::begin(EventLog* events, MenuStore* menu) {
    events_ = events;
    menu_ = menu;
    g_msc = this;
    changeMs_ = millis();
    loadDefaultSlots();

    if (DEMO_FAT_SECTOR_COUNT == 0 || DEMO_FAT_SIZE < 512) {
        if (events_) events_->push("msc.fail", "empty image");
        return false;
    }

    USB.onEvent(usb_event_cb);
    MSC.vendorID("PIDRIVE");
    MSC.productID("USB_MEDIA");
    MSC.productRevision("0.3");
    MSC.onStartStop(pidrive_msc_start_stop);
    MSC.onRead(pidrive_msc_read);
    MSC.onWrite(pidrive_msc_write);
    MSC.mediaPresent(true);
    if (!MSC.begin(DEMO_FAT_SECTOR_COUNT, DEMO_FAT_SECTOR_SIZE)) {
        if (events_) events_->push("msc.fail", "begin");
        return false;
    }
    USB.begin();
    ready_ = true;
    if (events_) events_->push("msc.ready", "FAT12 demo");
    Serial.printf("[MSC] ready sectors=%u\n", DEMO_FAT_SECTOR_COUNT);
    return true;
}

void UsbMscGadget::onUsbPlugged(bool on) {
    if (plugged_ == on) return;
    plugged_ = on;
    suspended_ = false;
    changeMs_ = millis();
    if (on) {
        plugMs_ = changeMs_;
        firstReadMs_ = 0;
        playGuessMs_ = 0;
        msPlugToFirstRead_ = 0;
        msPlugToPlayGuess_ = 0;
        readCount_ = 0;
        writeCount_ = 0;
        bytesRead_ = 0;
        bytesMeta_ = 0;
        bytesFile_ = 0;
        seqBytes_ = 0;
        seqFile_ = nullptr;
        prefetchHits_ = 0;
        traceHead_ = 0;
        traceCount_ = 0;
        plugCount_++;
        if (events_) events_->push("usb.otg.up", "car-host");
    } else {
        unplugCount_++;
        if (events_) events_->push("usb.otg.down", "car-host");
        if (menu_) menu_->clearPlaying();
    }
}

void UsbMscGadget::onUsbSuspend(bool on) {
    if (!plugged_ || suspended_ == on) return;
    suspended_ = on;
    if (events_) events_->push(on ? "usb.otg.suspend" : "usb.otg.resume", "car-host");
}

void UsbMscGadget::onHostStartStop(bool start, bool loadEject) {
    char det[40];
    snprintf(det, sizeof(det), "start=%u eject=%u", start ? 1 : 0, loadEject ? 1 : 0);
    if (events_ && millis() - lastEventMs_ > 200) {
        lastEventMs_ = millis();
        events_->push(start ? "msc.host.start" : "msc.host.stop", det);
    }
}

void UsbMscGadget::pushTrace(uint32_t lba, uint32_t bufsize, uint8_t kind, const char* tag) {
    MscReadSample& s = trace_[traceHead_];
    s.ms = millis();
    s.lba = lba;
    s.bytes = (uint16_t)(bufsize > 65535 ? 65535 : bufsize);
    s.kind = kind;
    strncpy(s.tag, tag ? tag : "", sizeof(s.tag) - 1);
    s.tag[sizeof(s.tag) - 1] = 0;
    traceHead_ = (traceHead_ + 1) % kTraceSize;
    if (traceCount_ < kTraceSize) traceCount_++;
}

UsbMscGadget::Region UsbMscGadget::classify(uint32_t lba, const MscFileMap** fileOut) const {
    if (fileOut) *fileOut = nullptr;
    if (lba < kDataStartLba) return Region::Meta;
    if (isDirLba(lba)) return Region::Dir;
    const MscFileMap* f = fileForLba(lba);
    if (f && f->active) {
        if (fileOut) *fileOut = f;
        return Region::File;
    }
    return Region::Meta;
}

bool UsbMscGadget::looksLikePlay(const MscFileMap* f, uint32_t startLba, uint32_t seqBytes) const {
    if (!f || !f->active || !f->uid[0]) return false;
    const bool fromHead = startLba <= f->lbaStart + 2;
    if (!fromHead) return false;
    if (seqBytes < 8192) return false;
    if (plugMs_ && (millis() - plugMs_) < 1500 && seqBytes < 16384) return false;
    return true;
}

int32_t UsbMscGadget::onWrite(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize) {
    (void)offset;
    (void)buffer;
    writeCount_++;
    pushTrace(lba, bufsize, 3, "WR");
    if (events_ && millis() - lastEventMs_ > 300) {
        lastEventMs_ = millis();
        char d[40];
        snprintf(d, sizeof(d), "lba=%u n=%u", (unsigned)lba, (unsigned)bufsize);
        events_->push("msc.write", d);
    }
    return (int32_t)bufsize;
}

const MscFileMap* UsbMscGadget::fileForLba(uint32_t lba) const {
    if (streamSlot_ >= 0 && lba >= streamLba0_ && lba <= streamLba1_) {
        return &slots_[streamSlot_];
    }
    for (size_t i = 0; i < kSlots; i++) {
        if (lba >= slots_[i].lbaStart && lba <= slots_[i].lbaEnd) return &slots_[i];
    }
    return nullptr;
}

void UsbMscGadget::noteDataRead(uint32_t lba, uint32_t bufsize) {
    readCount_++;
    lastReadLba_ = lba;
    bytesRead_ += bufsize;

    const MscFileMap* f = nullptr;
    Region reg = classify(lba, &f);
    const char* tag = "META";
    uint8_t kind = 0;
    if (reg == Region::Dir) {
        tag = "DIR";
        kind = 1;
        bytesMeta_ += bufsize;
    } else if (reg == Region::File && f) {
        tag = f->name;
        kind = 2;
        bytesFile_ += bufsize;
    } else {
        bytesMeta_ += bufsize;
    }
    pushTrace(lba, bufsize, kind, tag);

    if (millis() - lastTraceLogMs_ > 250) {
        lastTraceLogMs_ = millis();
        Serial.printf("[MSC] RD lba=%u n=%u %s\n", (unsigned)lba, (unsigned)bufsize, tag);
    }

    if (plugged_ && firstReadMs_ == 0) {
        firstReadMs_ = millis();
        msPlugToFirstRead_ = firstReadMs_ - plugMs_;
        if (events_) {
            char d[40];
            snprintf(d, sizeof(d), "%lu ms", (unsigned long)msPlugToFirstRead_);
            events_->push("msc.first_read", d);
        }
    }

    if (reg != Region::File || !f) {
        seqFile_ = nullptr;
        seqBytes_ = 0;
        return;
    }

    if (f != seqFile_ || lba < seqLba_ || (seqLba_ && lba > seqLba_ + 16)) {
        seqFile_ = f;
        seqBytes_ = 0;
        seqStartLba_ = lba;
    }
    seqLba_ = lba;
    seqBytes_ += bufsize;

    if (seqBytes_ >= 2048 && !looksLikePlay(f, seqStartLba_, seqBytes_)) {
        if (seqStartLba_ > f->lbaStart + 2) {
            prefetchHits_++;
            if (events_ && millis() - lastEventMs_ > 800) {
                lastEventMs_ = millis();
                events_->push("msc.prefetch", f->uid);
            }
        }
        return;
    }

    if (looksLikePlay(f, seqStartLba_, seqBytes_)) {
        bool fresh = !menu_ || strcmp(menu_->playingUid(), f->uid) != 0;
        if (menu_) menu_->playByUid(f->uid);
        if (fresh) {
            if (playGuessMs_ == 0 && plugMs_) {
                playGuessMs_ = millis();
                msPlugToPlayGuess_ = playGuessMs_ - plugMs_;
            }
            if (events_ && millis() - lastEventMs_ > 400) {
                lastEventMs_ = millis();
                events_->push("play.guess", f->uid);
                char d[56];
                snprintf(d, sizeof(d), "from=%u +%luB", (unsigned)seqStartLba_, (unsigned long)seqBytes_);
                events_->push("msc.stream", d);
            }
            if (playHandler_) playHandler_(f->uid);
        }
    }
}

void UsbMscGadget::loop() {}

uint32_t UsbMscGadget::msSincePlug() const {
    if (!plugged_ || !plugMs_) return 0;
    return millis() - plugMs_;
}

uint32_t UsbMscGadget::msSinceChange() const {
    if (!changeMs_) return 0;
    return millis() - changeMs_;
}

void UsbMscGadget::traceToJson(JsonArray arr) const {
    if (traceCount_ == 0) return;
    size_t start = (traceHead_ + kTraceSize - traceCount_) % kTraceSize;
    for (size_t i = 0; i < traceCount_; i++) {
        const MscReadSample& s = trace_[(start + i) % kTraceSize];
        JsonObject o = arr.add<JsonObject>();
        o["ms"] = s.ms;
        o["lba"] = s.lba;
        o["n"] = s.bytes;
        o["kind"] = s.kind == 3 ? "wr" : (s.kind == 2 ? "file" : (s.kind == 1 ? "dir" : "meta"));
        o["tag"] = s.tag;
    }
}

void UsbMscGadget::toJson(JsonObject obj) const {
    obj["ready"] = ready_;
    obj["plugged"] = plugged_;
    obj["suspended"] = suspended_;
    obj["readCount"] = readCount_;
    obj["writeCount"] = writeCount_;
    obj["lastReadLba"] = lastReadLba_;
    obj["bytesRead"] = bytesRead_;
    obj["bytesMeta"] = bytesMeta_;
    obj["bytesFile"] = bytesFile_;
    obj["prefetchHits"] = prefetchHits_;
    obj["streamSlot"] = streamSlot_;
    obj["streamBytes"] = streamBytesServed_;
    if (stream_) {
        JsonObject s = obj["stream"].to<JsonObject>();
        stream_->toJson(s);
    }
    obj["msSincePlug"] = msSincePlug();
    obj["msSinceChange"] = msSinceChange();
    obj["msPlugToFirstRead"] = msPlugToFirstRead_;
    obj["msPlugToPlayGuess"] = msPlugToPlayGuess_;
    obj["plugCount"] = plugCount_;
    obj["unplugCount"] = unplugCount_;
    obj["sectorCount"] = DEMO_FAT_SECTOR_COUNT;
    obj["imageBytes"] = DEMO_FAT_SIZE;
    obj["dataStartLba"] = kDataStartLba;
    obj["slots"] = (int)kSlots;
    obj["port"] = "otg";
    obj["role"] = "car-host";
    JsonArray arr = obj["slotMap"].to<JsonArray>();
    for (size_t i = 0; i < kSlots; i++) {
        JsonObject o = arr.add<JsonObject>();
        o["i"] = (int)i;
        o["uid"] = slots_[i].uid;
        o["name"] = slots_[i].name;
        o["lba0"] = slots_[i].lbaStart;
        o["lba1"] = slots_[i].lbaEnd;
        o["active"] = slots_[i].active;
    }
}

#else

bool UsbMscGadget::begin(EventLog* events, MenuStore* menu) {
    events_ = events;
    menu_ = menu;
    if (events_) events_->push("msc.fail", "ARDUINO_USB_MODE=1");
    return false;
}
void UsbMscGadget::loop() {}
void UsbMscGadget::applyMenuSlots(const MenuStore&) {}
void UsbMscGadget::startStream(const char*) {}
void UsbMscGadget::stopStream() {}
void UsbMscGadget::onUsbPlugged(bool) {}
void UsbMscGadget::onUsbSuspend(bool) {}
void UsbMscGadget::onHostStartStop(bool, bool) {}
int32_t UsbMscGadget::onRead(uint32_t, uint32_t, void*, uint32_t) { return -1; }
int32_t UsbMscGadget::onWrite(uint32_t, uint32_t, uint8_t*, uint32_t) { return -1; }
uint32_t UsbMscGadget::msSincePlug() const { return 0; }
uint32_t UsbMscGadget::msSinceChange() const { return 0; }
void UsbMscGadget::toJson(JsonObject obj) const {
    obj["ready"] = false;
    obj["error"] = "USB_MODE";
}
void UsbMscGadget::traceToJson(JsonArray) const {}
void UsbMscGadget::noteDataRead(uint32_t, uint32_t) {}
void UsbMscGadget::pushTrace(uint32_t, uint32_t, uint8_t, const char*) {}
UsbMscGadget::Region UsbMscGadget::classify(uint32_t, const MscFileMap**) const { return Region::Meta; }
const MscFileMap* UsbMscGadget::fileForLba(uint32_t) const { return nullptr; }
bool UsbMscGadget::looksLikePlay(const MscFileMap*, uint32_t, uint32_t) const { return false; }
void UsbMscGadget::loadDefaultSlots() {}
void UsbMscGadget::patchDirForStream(uint8_t*, uint32_t) const {}
void UsbMscGadget::patchFatForStream(uint8_t*, uint32_t) const {}

#endif
