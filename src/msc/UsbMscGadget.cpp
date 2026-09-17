#include "UsbMscGadget.h"
#include "DemoFatImage.h"
#include "USB.h"
#include "USBMSC.h"
#include <cstring>

#if !ARDUINO_USB_MODE

static UsbMscGadget* g_msc = nullptr;
static USBMSC MSC;

// LBA map from demo FAT12 (cluster→LBA verified against demo_fat.bin)
static const MscFileMap kFiles[] = {
    {43, 58, "demo:rock_fm", "ROCK FM", "STATIONS/01ROCK.MP3"},
    {59, 74, "demo:antenne", "Antenne 1", "STATIONS/02ANTENN.MP3"},
    {75, 90, "demo:swr3", "SWR3", "STATIONS/03SWR3.MP3"},
    {91, 102, "action:about", "About", "SETTINGS/ABOUT.MP3"},
};

// Directory cluster LBAs (spc=4): STATIONS=35-38, SETTINGS=39-42
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
        case ARDUINO_USB_STARTED_EVENT:
            g_msc->onUsbPlugged(true);
            break;
        case ARDUINO_USB_STOPPED_EVENT:
            g_msc->onUsbPlugged(false);
            break;
        case ARDUINO_USB_SUSPEND_EVENT:
            g_msc->onUsbSuspend(true);
            break;
        case ARDUINO_USB_RESUME_EVENT:
            g_msc->onUsbSuspend(false);
            break;
        default:
            break;
    }
}

bool UsbMscGadget::begin(EventLog* events, MenuStore* menu) {
    events_ = events;
    menu_ = menu;
    g_msc = this;
    changeMs_ = millis();

    if (DEMO_FAT_SECTOR_COUNT == 0 || DEMO_FAT_SIZE < 512) {
        if (events_) events_->push("msc.fail", "empty image");
        return false;
    }

    USB.onEvent(usb_event_cb);
    MSC.vendorID("PIDRIVE");
    MSC.productID("USB_MEDIA");
    MSC.productRevision("0.2");
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
    Serial.printf("[MSC] ready sectors=%u size=%u\n", DEMO_FAT_SECTOR_COUNT, DEMO_FAT_SIZE);
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
        Serial.println("[OTG] UP car-host mounted");
    } else {
        unplugCount_++;
        if (events_) events_->push("usb.otg.down", "car-host");
        Serial.println("[OTG] DOWN car-host gone");
        if (menu_) menu_->clearPlaying();
    }
}

void UsbMscGadget::onUsbSuspend(bool on) {
    if (!plugged_) return;
    if (suspended_ == on) return;
    suspended_ = on;
    if (events_) events_->push(on ? "usb.otg.suspend" : "usb.otg.resume", "car-host");
    Serial.printf("[OTG] %s\n", on ? "SUSPEND" : "RESUME");
}

void UsbMscGadget::onHostStartStop(bool start, bool loadEject) {
    char det[40];
    snprintf(det, sizeof(det), "start=%u eject=%u", start ? 1 : 0, loadEject ? 1 : 0);
    Serial.printf("[MSC] START/STOP %s\n", det);
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
    if (f) {
        if (fileOut) *fileOut = f;
        return Region::File;
    }
    return Region::Meta;
}

bool UsbMscGadget::looksLikePlay(const MscFileMap* f, uint32_t startLba, uint32_t seqBytes) const {
    if (!f) return false;
    // PC/Linux probe often touches mid-file with 4 KiB — require start near file head
    // and a longer sequential run (PC-Test 2026-09-17: false play.guess @ lba=64).
    const bool fromHead = startLba <= f->lbaStart + 2;
    if (!fromHead) return false;
    if (seqBytes < 8192) return false;
    // Ignore mount storm in first 1.5 s unless stream is already long
    if (plugMs_ && (millis() - plugMs_) < 1500 && seqBytes < 16384) return false;
    return true;
}

int32_t UsbMscGadget::onRead(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize) {
    if (lba >= DEMO_FAT_SECTOR_COUNT) return -1;
    uint32_t pos = lba * DEMO_FAT_SECTOR_SIZE + offset;
    if (pos >= DEMO_FAT_SIZE) return -1;
    uint32_t avail = DEMO_FAT_SIZE - pos;
    if (bufsize > avail) bufsize = avail;
    memcpy(buffer, DEMO_FAT_IMAGE + pos, bufsize);
    noteDataRead(lba, bufsize);
    return (int32_t)bufsize;
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
    Serial.printf("[MSC] WRITE lba=%u n=%u (discard)\n", (unsigned)lba, (unsigned)bufsize);
    return (int32_t)bufsize;
}

const MscFileMap* UsbMscGadget::fileForLba(uint32_t lba) const {
    for (const auto& f : kFiles) {
        if (lba >= f.lbaStart && lba <= f.lbaEnd) return &f;
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
        const char* slash = strrchr(f->path, '/');
        tag = slash ? slash + 1 : f->path;
        kind = 2;
        bytesFile_ += bufsize;
    } else {
        bytesMeta_ += bufsize;
    }
    pushTrace(lba, bufsize, kind, tag);

    // Rate-limited serial + event for visibility during lab
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
        // new sequence (or big jump = not sequential stream)
        seqFile_ = f;
        seqBytes_ = 0;
        seqStartLba_ = lba;
    }
    seqLba_ = lba;
    seqBytes_ += bufsize;

    // Mid-file probe → prefetch, not play
    if (seqBytes_ >= 2048 && !looksLikePlay(f, seqStartLba_, seqBytes_)) {
        if (seqStartLba_ > f->lbaStart + 2) {
            prefetchHits_++;
            if (events_ && millis() - lastEventMs_ > 800) {
                lastEventMs_ = millis();
                char d[48];
                snprintf(d, sizeof(d), "%s lba=%u", f->uid, (unsigned)seqStartLba_);
                events_->push("msc.prefetch", d);
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
    obj["msSincePlug"] = msSincePlug();
    obj["msSinceChange"] = msSinceChange();
    obj["msPlugToFirstRead"] = msPlugToFirstRead_;
    obj["msPlugToPlayGuess"] = msPlugToPlayGuess_;
    obj["plugCount"] = plugCount_;
    obj["unplugCount"] = unplugCount_;
    obj["sectorCount"] = DEMO_FAT_SECTOR_COUNT;
    obj["imageBytes"] = DEMO_FAT_SIZE;
    obj["dataStartLba"] = kDataStartLba;
    obj["port"] = "otg";
    obj["role"] = "car-host";
}

#else

bool UsbMscGadget::begin(EventLog* events, MenuStore* menu) {
    events_ = events;
    menu_ = menu;
    if (events_) events_->push("msc.fail", "ARDUINO_USB_MODE=1");
    Serial.println("[MSC] disabled: need ARDUINO_USB_MODE=0 (TinyUSB OTG)");
    return false;
}
void UsbMscGadget::loop() {}
void UsbMscGadget::onUsbPlugged(bool) {}
void UsbMscGadget::onUsbSuspend(bool) {}
void UsbMscGadget::onHostStartStop(bool, bool) {}
int32_t UsbMscGadget::onRead(uint32_t, uint32_t, void*, uint32_t) { return -1; }
int32_t UsbMscGadget::onWrite(uint32_t, uint32_t, uint8_t*, uint32_t) { return -1; }
uint32_t UsbMscGadget::msSincePlug() const { return 0; }
uint32_t UsbMscGadget::msSinceChange() const { return 0; }
void UsbMscGadget::toJson(JsonObject obj) const {
    obj["ready"] = false;
    obj["plugged"] = false;
    obj["error"] = "USB_MODE";
}
void UsbMscGadget::traceToJson(JsonArray) const {}
void UsbMscGadget::noteDataRead(uint32_t, uint32_t) {}
void UsbMscGadget::pushTrace(uint32_t, uint32_t, uint8_t, const char*) {}
UsbMscGadget::Region UsbMscGadget::classify(uint32_t, const MscFileMap**) const { return Region::Meta; }
const MscFileMap* UsbMscGadget::fileForLba(uint32_t) const { return nullptr; }
bool UsbMscGadget::looksLikePlay(const MscFileMap*, uint32_t, uint32_t) const { return false; }

#endif
