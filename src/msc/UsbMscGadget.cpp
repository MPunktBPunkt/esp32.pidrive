#include "UsbMscGadget.h"
#include "DemoFatImage.h"
#include "USB.h"
#include "USBMSC.h"
#include <cstring>

#if !ARDUINO_USB_MODE

static UsbMscGadget* g_msc = nullptr;
static USBMSC MSC;

// TinyUSB weak hook — present volume as write-protected.
extern "C" bool tud_msc_is_writable_cb(uint8_t lun) {
    (void)lun;
    return false;
}

static bool isDirLba(uint32_t lba) {
    return lba >= 35 && lba <= 42;
}

static int32_t pidrive_msc_read(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize) {
    if (!g_msc) return 0;
    return g_msc->onRead(lba, offset, buffer, bufsize);
}

static int32_t pidrive_msc_write(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize) {
    if (!g_msc) return -1;
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
    // Fixed disjoint geometry — never mutated by startStream/stopStream.
    const char* names[] = {"ROCK FM", "Antenne 1", "SWR3", "About"};
    const char* uids[] = {"demo:rock_fm", "demo:antenne", "demo:swr3", "action:about"};
    const char* paths[] = {
        "STATIONS/01ROCK.MP3", "STATIONS/02ANTENN.MP3", "STATIONS/03SWR3.MP3", "SETTINGS/ABOUT.MP3"};
    // ~64 KiB each station (stub + pad for play.guess); settings short
    const uint32_t ranges[][2] = {{43, 170}, {171, 298}, {299, 426}, {427, 438}};
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
    // Preserve geometry; only refresh uid/name. Do not touch streamSlot_.
    const uint32_t ranges[][2] = {{43, 170}, {171, 298}, {299, 426}, {427, 438}};
    size_t n = menu.count();
    if (n > kSlots) n = kSlots;
    for (size_t i = 0; i < kSlots; i++) {
        slots_[i].lbaStart = ranges[i][0];
        slots_[i].lbaEnd = ranges[i][1];
        if (i < n) {
            const MenuItem* it = menu.itemAt(i);
            if (!it) continue;
            strncpy(slots_[i].uid, it->uid, sizeof(slots_[i].uid) - 1);
            slots_[i].uid[sizeof(slots_[i].uid) - 1] = 0;
            strncpy(slots_[i].name, it->name, sizeof(slots_[i].name) - 1);
            slots_[i].name[sizeof(slots_[i].name) - 1] = 0;
            slots_[i].active = true;
        } else {
            slots_[i].active = false;
            slots_[i].uid[0] = 0;
            slots_[i].name[0] = 0;
        }
    }
    // Re-resolve stream slot index if uid still present (geometry unchanged).
    if (stream_ && stream_->active() && stream_->uid()[0]) {
        startStream(stream_->uid());
    }
    if (events_) {
        char d[32];
        snprintf(d, sizeof(d), "slots=%u", (unsigned)n);
        events_->push("msc.slots", d);
    }
    Serial.printf("[MSC] applyMenuSlots n=%u (static FAT)\n", (unsigned)n);
}

void UsbMscGadget::startStream(const char* uid) {
    streamSlot_ = -1;
    if (!uid || !uid[0]) return;
    for (size_t i = 0; i < kSlots; i++) {
        if (slots_[i].active && strcmp(slots_[i].uid, uid) == 0) {
            streamSlot_ = (int)i;
            // FAT/dir/size untouched — only payload overlay changes.
            Serial.printf("[MSC] stream overlay slot=%d lba=%u..%u uid=%s\n", streamSlot_,
                          (unsigned)slots_[i].lbaStart, (unsigned)slots_[i].lbaEnd, uid);
            if (events_) events_->push("msc.stream_on", uid);
            return;
        }
    }
    Serial.printf("[MSC] stream uid not in slots: %s\n", uid);
}

void UsbMscGadget::stopStream() {
    if (streamSlot_ >= 0 && events_) events_->push("msc.stream_off", "");
    streamSlot_ = -1;
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

void UsbMscGadget::patchFatChain(uint8_t* sector, uint32_t lba, uint16_t cl0, uint16_t cl1) const {
    if (cl0 < 2 || cl1 < cl0) return;
    // Demo FAT: reserved=1, fats=2, fatz=2 → FAT0 at LBA 1-2, FAT1 at LBA 3-4
    if (lba != 1 && lba != 2 && lba != 3 && lba != 4) return;

    uint32_t fatIndex = (lba == 1 || lba == 3) ? 0 : 1;
    uint32_t fatBase = fatIndex * 512;

    for (uint16_t cl = cl0; cl <= cl1; cl++) {
        uint16_t next = (cl < cl1) ? (uint16_t)(cl + 1) : 0xFFF;
        size_t byteIndex = cl + (cl / 2);
        size_t local = byteIndex - fatBase;
        if (local < 511) {
            uint8_t pair[2] = {sector[local], sector[local + 1]};
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
            if (cl & 1) {
                sector[511] = (uint8_t)((sector[511] & 0x0F) | ((next & 0x0F) << 4));
            } else {
                sector[511] = (uint8_t)(next & 0xFF);
            }
        }
    }
    (void)fat12Set;
}

void UsbMscGadget::patchFatFixed(uint8_t* sector, uint32_t lba) const {
    // Always the same chains — independent of stream overlay.
    for (size_t i = 0; i < kSlots; i++) {
        if (!slots_[i].active) continue;
        uint16_t cl0 = lbaToCluster(slots_[i].lbaStart);
        uint16_t cl1 = lbaToCluster(slots_[i].lbaEnd);
        patchFatChain(sector, lba, cl0, cl1);
    }
}

static uint8_t fat83Checksum(const uint8_t name83[11]) {
    uint8_t sum = 0;
    for (int i = 0; i < 11; i++) {
        sum = (uint8_t)(((sum & 1) ? 0x80 : 0) + (sum >> 1) + name83[i]);
    }
    return sum;
}

/** Map label to FAT 8.3 (name + "MP3"), uppercase ASCII. */
static void labelTo83(const char* label, uint8_t out[11]) {
    memset(out, ' ', 11);
    out[8] = 'M';
    out[9] = 'P';
    out[10] = '3';
    if (!label) label = "TRACK";
    int n = 0;
    for (const char* p = label; *p && n < 8; ++p) {
        unsigned char c = (unsigned char)*p;
        if (c >= 'a' && c <= 'z') c = (unsigned char)(c - 'a' + 'A');
        // fold a few common non-ASCII
        if (c == 0xC3) continue;  // UTF-8 lead — skip, next byte handled loosely
        if (c & 0x80) {
            // rough: ü/ä/ö → U/A/O if we see latin1-ish
            if (c == 0xFC || c == 0xDC) c = 'U';
            else if (c == 0xE4 || c == 0xC4) c = 'A';
            else if (c == 0xF6 || c == 0xD6) c = 'O';
            else if (c == 0xDF) { /* ß */ if (n < 7) { out[n++] = 'S'; out[n++] = 'S'; } continue; }
            else continue;
        }
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
            out[n++] = c;
        }
    }
    if (n == 0) {
        memcpy(out, "TRACK   ", 8);
    }
}

/** Write one LFN entry (13 UTF-16 code units). ord = 1..N, last has 0x40. */
static void writeLfnEntry(uint8_t* ent, uint8_t ord, bool last, const uint16_t* u16, int u16Len,
                          uint8_t checksum) {
    memset(ent, 0xFF, 32);
    ent[0] = (uint8_t)(ord | (last ? 0x40 : 0));
    ent[11] = 0x0F;
    ent[12] = 0;
    ent[13] = checksum;
    ent[26] = 0;
    ent[27] = 0;
    // name1[5] at 1, name2[6] at 14, name3[2] at 28
    const int pos[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
    for (int i = 0; i < 13; i++) {
        int idx = (ord - 1) * 13 + i;
        uint16_t ch;
        if (idx < u16Len) ch = u16[idx];
        else if (idx == u16Len) ch = 0;
        else ch = 0xFFFF;
        ent[pos[i]] = (uint8_t)(ch & 0xFF);
        ent[pos[i] + 1] = (uint8_t)(ch >> 8);
    }
}

static int labelToUtf16(const char* label, uint16_t* out, int maxOut) {
    if (!label) label = "Track";
    int n = 0;
    const unsigned char* p = (const unsigned char*)label;
    while (*p && n < maxOut) {
        unsigned char c = *p++;
        if (c < 0x80) {
            out[n++] = c;
        } else if ((c & 0xE0) == 0xC0 && *p) {
            unsigned char c2 = *p++;
            out[n++] = (uint16_t)(((c & 0x1F) << 6) | (c2 & 0x3F));
        } else if ((c & 0xF0) == 0xE0 && p[0] && p[1]) {
            unsigned char c2 = *p++;
            unsigned char c3 = *p++;
            out[n++] = (uint16_t)(((c & 0x0F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F));
        } else {
            out[n++] = '?';
        }
    }
    return n;
}

static void write83File(uint8_t* ent, const uint8_t name83[11], uint16_t startCl, uint32_t size) {
    memset(ent, 0, 32);
    memcpy(ent, name83, 11);
    ent[11] = 0x20;  // archive
    ent[26] = (uint8_t)(startCl & 0xFF);
    ent[27] = (uint8_t)(startCl >> 8);
    ent[28] = (uint8_t)(size & 0xFF);
    ent[29] = (uint8_t)((size >> 8) & 0xFF);
    ent[30] = (uint8_t)((size >> 16) & 0xFF);
    ent[31] = (uint8_t)((size >> 24) & 0xFF);
}

static void writeDotDirs(uint8_t* sector, uint16_t selfCl) {
    memset(sector, 0, 512);
    // .
    memset(sector, ' ', 11);
    sector[0] = '.';
    sector[11] = 0x10;
    sector[26] = (uint8_t)(selfCl & 0xFF);
    sector[27] = (uint8_t)(selfCl >> 8);
    // ..
    memset(sector + 32, ' ', 11);
    sector[32] = '.';
    sector[33] = '.';
    sector[32 + 11] = 0x10;
}

void UsbMscGadget::patchDirNames(uint8_t* sector, uint32_t lba) const {
    // STATIONS = cluster 2 → LBA 35; SETTINGS = cluster 3 → LBA 39
    const bool stations = (lba == 35);
    const bool settings = (lba == 39);
    if (!stations && !settings) return;

    if (stations) {
        writeDotDirs(sector, 2);
        int ent = 2;
        for (int slot = 0; slot < 3 && ent < 16; slot++) {
            if (!slots_[slot].active || !slots_[slot].name[0]) continue;

            uint8_t name83[11];
            labelTo83(slots_[slot].name, name83);
            uint8_t sum = fat83Checksum(name83);

            uint16_t u16[40];
            int u16Len = labelToUtf16(slots_[slot].name, u16, 39);
            if (u16Len < 36) {
                const char* ext = ".mp3";
                for (int i = 0; ext[i] && u16Len < 39; i++) u16[u16Len++] = (uint16_t)ext[i];
            }
            int nLfn = (u16Len + 12) / 13;
            if (nLfn < 1) nLfn = 1;
            if (ent + nLfn + 1 > 16) break;

            for (int ord = nLfn; ord >= 1; --ord) {
                writeLfnEntry(sector + ent * 32, (uint8_t)ord, ord == nLfn, u16, u16Len, sum);
                ent++;
            }

            uint16_t startCl = lbaToCluster(slots_[slot].lbaStart);
            uint32_t fileBytes = slotBytes(slots_[slot]);
            write83File(sector + ent * 32, name83, startCl, fileBytes);
            ent++;
        }
        return;
    }

    if (settings && slots_[3].active && slots_[3].name[0]) {
        writeDotDirs(sector, 3);
        uint8_t name83[11];
        labelTo83(slots_[3].name, name83);
        uint8_t sum = fat83Checksum(name83);
        uint16_t u16[40];
        int u16Len = labelToUtf16(slots_[3].name, u16, 39);
        if (u16Len < 36) {
            const char* ext = ".mp3";
            for (int i = 0; ext[i] && u16Len < 39; i++) u16[u16Len++] = (uint16_t)ext[i];
        }
        int nLfn = (u16Len + 12) / 13;
        if (nLfn < 1) nLfn = 1;
        int ent = 2;
        for (int ord = nLfn; ord >= 1 && ent < 15; --ord) {
            writeLfnEntry(sector + ent * 32, (uint8_t)ord, ord == nLfn, u16, u16Len, sum);
            ent++;
        }
        uint16_t startCl = lbaToCluster(slots_[3].lbaStart);
        write83File(sector + ent * 32, name83, startCl, slotBytes(slots_[3]));
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
    // Static view: names + fixed FAT chains (identical whether streaming or not).
    if (offset == 0 && bufsize >= 512) {
        for (uint32_t off = 0; off + 512 <= bufsize; off += 512) {
            uint32_t sec = lba + off / 512;
            patchDirNames(out + off, sec);
            patchFatFixed(out + off, sec);
        }
    }

    // File payload: live ringbuffer for active stream slot, else stub/pad.
    static const uint32_t kStubLba[3] = {43, 59, 75};
    const MscFileMap* f = fileForLba(lba);
    if (f) {
        const bool live = stream_ && stream_->active() && streamSlot_ >= 0 && f == &slots_[streamSlot_];
        if (live) {
            uint32_t fileOff = (lba - f->lbaStart) * DEMO_FAT_SECTOR_SIZE + offset;
            stream_->readAt(fileOff, out, bufsize);
            streamBytesServed_ += bufsize;
        } else if (f >= &slots_[0] && f <= &slots_[2]) {
            size_t i = (size_t)(f - &slots_[0]);
            uint32_t rel = lba - f->lbaStart;
            if (rel < 16) {
                uint32_t src = kStubLba[i] + rel;
                uint32_t spos = src * DEMO_FAT_SECTOR_SIZE + offset;
                if (spos + bufsize <= DEMO_FAT_SIZE) {
                    memcpy(out, DEMO_FAT_IMAGE + spos, bufsize);
                }
            } else {
                memset(out, 0xFF, bufsize);
            }
        }
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
    MSC.productRevision("0.4");
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
    if (events_) events_->push("msc.ready", "FAT12 static");
    Serial.printf("[MSC] ready sectors=%u static-FAT\n", DEMO_FAT_SECTOR_COUNT);
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
        streamBytesServed_ = 0;
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
    // Hosts often 4 KiB-align; first USB read may start a few LBAs after file start.
    const bool fromHead = startLba <= f->lbaStart + 12;
    if (!fromHead) return false;
    // Demo stubs are ~6.5 KiB. BMW indexes the whole stub in the first 1–2 s after
    // plug — that must NOT arm live stream (else NBT: „keine abspielbaren Titel“).
    // Real play usually re-reads from the head after the index window.
    if (plugMs_ && (millis() - plugMs_) < 2500) return false;
    // Slightly under stub size so a full re-read of 01ROCK.MP3 (6495 B) counts as play.
    if (seqBytes < 6000) return false;
    return true;
}

int32_t UsbMscGadget::onWrite(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize) {
    (void)offset;
    (void)buffer;
    writeCount_++;
    writeRejectCount_++;
    pushTrace(lba, bufsize, 3, "WR!");
    if (events_ && millis() - lastEventMs_ > 500) {
        lastEventMs_ = millis();
        char d[48];
        snprintf(d, sizeof(d), "lba=%u n=%u (ro)", (unsigned)lba, (unsigned)bufsize);
        events_->push("msc.write_reject", d);
    }
    // Negative → TinyUSB fails the WRITE; combined with tud_msc_is_writable_cb=false.
    return -1;
}

const MscFileMap* UsbMscGadget::fileForLba(uint32_t lba) const {
    for (size_t i = 0; i < kSlots; i++) {
        if (!slots_[i].active) continue;
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
        // Mid-file prefetch only — from-head reads must keep accumulating so a
        // phone that reads past the stub after the plug-window can still arm.
        if (seqStartLba_ > f->lbaStart + 2) {
            prefetchHits_++;
            if (events_ && millis() - lastEventMs_ > 800) {
                lastEventMs_ = millis();
                events_->push("msc.prefetch", f->uid);
            }
            return;
        }
    }

    if (looksLikePlay(f, seqStartLba_, seqBytes_)) {
        // One live-stream arm per plug window — neighboring stubs are often
        // touched right after the first play and must not steal the stream.
        if (playGuessMs_ != 0 && (millis() - playGuessMs_) < 5000) {
            return;
        }
        bool fresh = !menu_ || strcmp(menu_->playingUid(), f->uid) != 0;
        if (menu_) menu_->playByUid(f->uid);
        if (fresh) {
            if (plugMs_) {
                playGuessMs_ = millis();
                msPlugToPlayGuess_ = playGuessMs_ - plugMs_;
            } else {
                playGuessMs_ = millis();
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
    obj["writeReject"] = writeRejectCount_;
    obj["lastReadLba"] = lastReadLba_;
    obj["bytesRead"] = bytesRead_;
    obj["bytesMeta"] = bytesMeta_;
    obj["bytesFile"] = bytesFile_;
    obj["prefetchHits"] = prefetchHits_;
    obj["streamSlot"] = streamSlot_;
    obj["streamBytes"] = streamBytesServed_;
    obj["fatMode"] = "static";
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
void UsbMscGadget::patchDirNames(uint8_t*, uint32_t) const {}
void UsbMscGadget::patchFatFixed(uint8_t*, uint32_t) const {}
void UsbMscGadget::patchFatChain(uint8_t*, uint32_t, uint16_t, uint16_t) const {}

#endif
