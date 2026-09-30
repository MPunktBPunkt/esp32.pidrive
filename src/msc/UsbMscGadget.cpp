#include "UsbMscGadget.h"
#include "DemoFatImage.h"
#include "HostScsiProbe.h"
#include "USB.h"
#include "USBMSC.h"
#include <Preferences.h>
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
    // Root 17-48; STATIONS/SETTINGS clusters 49-56.
    if (lba >= UsbMscGadget::kRootLba0 &&
        lba < UsbMscGadget::kRootLba0 + UsbMscGadget::kRootSectors)
        return true;
    return (lba >= UsbMscGadget::kStationsLba && lba < UsbMscGadget::kStationsLba + UsbMscGadget::kSpc) ||
           (lba >= UsbMscGadget::kSettingsLba && lba < UsbMscGadget::kSettingsLba + UsbMscGadget::kSpc);
}

/** Virtual FAT12: boot@0, FAT0@1-8, FAT1@9-16, ROOT@17-48, DATA@49. */
static const char* metaTag(uint32_t lba) {
    if (lba == 0) return "BOOT";
    if (lba >= UsbMscGadget::kFat0Lba && lba < UsbMscGadget::kFat0Lba + UsbMscGadget::kFatSpf)
        return "FAT0";
    if (lba >= UsbMscGadget::kFat1Lba && lba < UsbMscGadget::kFat1Lba + UsbMscGadget::kFatSpf)
        return "FAT1";
    if (lba >= UsbMscGadget::kRootLba0 &&
        lba < UsbMscGadget::kRootLba0 + UsbMscGadget::kRootSectors)
        return "ROOT";
    return "META";
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
    if (g_msc) g_msc->onHostStartStop(power_condition, start, load_eject);
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
    const char* kinds[] = {"station", "station", "station", "action"};
    const char* paths[] = {
        "STATIONS/01ROCK.MP3", "STATIONS/02ANTENN.MP3", "STATIONS/03SWR3.MP3", "SETTINGS/ABOUT.MP3"};
    uint32_t ranges[kSlots][2];
    slotRanges(ranges);
    for (size_t i = 0; i < kSlots; i++) {
        slots_[i].lbaStart = ranges[i][0];
        slots_[i].lbaEnd = ranges[i][1];
        strncpy(slots_[i].name, names[i], sizeof(slots_[i].name) - 1);
        strncpy(slots_[i].uid, uids[i], sizeof(slots_[i].uid) - 1);
        strncpy(slots_[i].path, paths[i], sizeof(slots_[i].path) - 1);
        strncpy(slots_[i].kind, kinds[i], sizeof(slots_[i].kind) - 1);
        slots_[i].active = true;
    }
}

void UsbMscGadget::applyMenuSlots(const MenuStore& menu) {
    // Preserve geometry; only refresh uid/name when content actually differs.
    uint32_t ranges[kSlots][2];
    slotRanges(ranges);
    size_t n = menu.count();
    if (n > kSlots) n = kSlots;

    bool same = true;
    size_t activeBefore = 0;
    for (size_t i = 0; i < kSlots; i++) {
        if (slots_[i].active) activeBefore++;
    }
    if (activeBefore != n) same = false;
    if (same) {
        for (size_t i = 0; i < n; i++) {
            const MenuItem* it = menu.itemAt(i);
            if (!it || !slots_[i].active || strcmp(slots_[i].uid, it->uid) != 0 ||
                strcmp(slots_[i].name, it->name) != 0) {
                same = false;
                break;
            }
        }
    }

    if (!same) {
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
                strncpy(slots_[i].kind, it->kind, sizeof(slots_[i].kind) - 1);
                slots_[i].kind[sizeof(slots_[i].kind) - 1] = 0;
                slots_[i].active = true;
            } else {
                slots_[i].active = false;
                slots_[i].uid[0] = 0;
                slots_[i].name[0] = 0;
                slots_[i].kind[0] = 0;
            }
        }
        if (events_) {
            char d[32];
            snprintf(d, sizeof(d), "slots=%u", (unsigned)n);
            events_->push("msc.slots", d);
        }
        Serial.printf("[MSC] applyMenuSlots n=%u (static FAT)\n", (unsigned)n);
        // Names live only in patched DIR sectors — host must re-read. Without remount
        // BMW/NBT keeps the first listing (often demo 01ROCK.* / stale NVS).
        if (mediaPresented_ && !(stream_ && stream_->active())) {
            remountMedia("menu");
        }
    }

    // Re-resolve stream slot index if uid still present (geometry unchanged).
    if (stream_ && stream_->active() && stream_->uid()[0]) {
        startStream(stream_->uid());
    }
    // mediaPresent is NOT toggled here — see presentMedia() / remountMedia().
}

void UsbMscGadget::formatPdSerial(uint16_t gen, char* ser, size_t n) {
    if (gen == 0) {
        snprintf(ser, n, "PD0000");
    } else {
        snprintf(ser, n, "PD%04u", (unsigned)(gen % 10000));
    }
}

void UsbMscGadget::loadRemountGenNvs() {
    Preferences prefs;
    if (!prefs.begin("pidrive", true)) return;
    remountGen_ = prefs.getUShort("rm_gen", 0);
    prefs.end();
}

void UsbMscGadget::saveRemountGenNvs() const {
    Preferences prefs;
    if (!prefs.begin("pidrive", false)) return;
    prefs.putUShort("rm_gen", remountGen_);
    prefs.end();
}

void UsbMscGadget::applyUsbIdentity() {
    remountGen_++;
    // Avoid wrapping back to 0 (volume label "PIDRIVE" / first-boot identity).
    if (remountGen_ == 0) remountGen_ = 1;
    char ser[12];
    formatPdSerial(remountGen_, ser, sizeof(ser));
    USB.serialNumber(ser);
    char rev[5];
    snprintf(rev, sizeof(rev), "%02u%02u", (unsigned)((remountGen_ / 100) % 100),
             (unsigned)(remountGen_ % 100));
    MSC.productRevision(rev);
    saveRemountGenNvs();
}

void UsbMscGadget::remountMedia(const char* reason) {
    if (!ready_) return;
    // Never yank the medium while live overlay is armed — kills TCP + host reads.
    if (stream_ && stream_->active()) {
        if (events_) events_->push("msc.remount_skip", "streaming");
        return;
    }
    // New USB identity so NBT MediaStore treats this as a different stick.
    applyUsbIdentity();
    char ser[12];
    formatPdSerial(remountGen_, ser, sizeof(ser));
    MSC.mediaPresent(false);
    mediaPresented_ = false;
    indexSettled_ = false;
    quietEmitted_ = false;
    // Longer not-ready: BMW needs time to drop the auto-play queue / DIR cache.
    remountHoldUntilMs_ = millis() + 600;
    presentDeadlineMs_ = remountHoldUntilMs_;
    if (events_) {
        char d[40];
        snprintf(d, sizeof(d), "%s ser=%s", reason ? reason : "", ser);
        events_->push("msc.remount", d);
    }
    Serial.printf("[MSC] remount scheduled (%s) ser=%s\n", reason ? reason : "", ser);
}

void UsbMscGadget::presentMedia(const char* reason) {
    if (mediaPresented_) return;
    if (remountHoldUntilMs_ && (int32_t)(millis() - remountHoldUntilMs_) < 0) {
        // Still in hide window (e.g. menu_set called presentMedia immediately).
        presentDeadlineMs_ = remountHoldUntilMs_;
        return;
    }
    MSC.mediaPresent(true);
    mediaPresented_ = true;
    presentDeadlineMs_ = 0;
    remountHoldUntilMs_ = 0;
    if (events_) events_->push("msc.media_on", reason ? reason : "");
    Serial.printf("[MSC] mediaPresent=true (%s)\n", reason ? reason : "");
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
    uint32_t fatBase;
    if (lba >= kFat0Lba && lba < kFat0Lba + kFatSpf) {
        fatBase = (lba - kFat0Lba) * 512u;
    } else if (lba >= kFat1Lba && lba < kFat1Lba + kFatSpf) {
        fatBase = (lba - kFat1Lba) * 512u;
    } else {
        return;
    }

    for (uint16_t cl = cl0; cl <= cl1; cl++) {
        uint16_t next = (cl < cl1) ? (uint16_t)(cl + 1) : 0xFFF;
        size_t byteIndex = cl + (cl / 2);
        if (byteIndex < fatBase) continue;
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
    // Directory clusters + slot chains — independent of stream overlay.
    patchFatChain(sector, lba, 2, 2);  // STATIONS
    patchFatChain(sector, lba, 3, 3);  // SETTINGS
    for (size_t i = 0; i < kSlots; i++) {
        if (!slots_[i].active) continue;
        uint16_t cl0 = lbaToCluster(slots_[i].lbaStart);
        uint16_t cl1 = lbaToCluster(slots_[i].lbaEnd);
        patchFatChain(sector, lba, cl0, cl1);
    }
}

void UsbMscGadget::patchBoot(uint8_t* sector) const {
    // Start from demo boot, then override BPB for virtual 2MiB / spf=8 geometry.
    memcpy(sector, DEMO_FAT_IMAGE, 512);
    sector[13] = kSpc;
    sector[14] = 1;
    sector[15] = 0;  // reserved = 1
    sector[16] = 2;  // FATs
    sector[17] = (uint8_t)(512 & 0xFF);
    sector[18] = (uint8_t)(512 >> 8);  // root entries
    sector[19] = (uint8_t)(kVirtSectorCount & 0xFF);
    sector[20] = (uint8_t)((kVirtSectorCount >> 8) & 0xFF);
    sector[22] = kFatSpf;
    sector[23] = 0;
    // total32 (unused when total16 != 0) clear
    sector[32] = 0;
    sector[33] = 0;
    sector[34] = 0;
    sector[35] = 0;
}

void UsbMscGadget::patchRootDir(uint8_t* sector, uint32_t lba) const {
    memset(sector, 0, 512);
    if (lba != kRootLba0) return;
    // Volume label — include remount gen so HU cache key changes (PIDRIVE / PD0001 …).
    memset(sector, ' ', 11);
    char vol[12];
    if (remountGen_ == 0) {
        snprintf(vol, sizeof(vol), "PIDRIVE");
    } else {
        snprintf(vol, sizeof(vol), "PD%04u", (unsigned)(remountGen_ % 10000));
    }
    size_t vl = strlen(vol);
    if (vl > 11) vl = 11;
    memcpy(sector, vol, vl);
    sector[11] = 0x08;
    // STATIONS
    memset(sector + 32, ' ', 11);
    memcpy(sector + 32, "STATIONS", 8);
    sector[32 + 11] = 0x10;
    sector[32 + 26] = 2;
    sector[32 + 27] = 0;
    // SETTINGS
    memset(sector + 64, ' ', 11);
    memcpy(sector + 64, "SETTINGS", 8);
    sector[64 + 11] = 0x10;
    sector[64 + 26] = 3;
    sector[64 + 27] = 0;
}

static uint8_t fat83Checksum(const uint8_t name83[11]) {
    uint8_t sum = 0;
    for (int i = 0; i < 11; i++) {
        sum = (uint8_t)(((sum & 1) ? 0x80 : 0) + (sum >> 1) + name83[i]);
    }
    return sum;
}

/** Strip chars illegal in FAT LFN/8.3: " * / : < > ? \ | and controls. */
static void sanitizeFatLabel(const char* in, char* out, size_t outSz) {
    if (!out || outSz == 0) return;
    out[0] = 0;
    if (!in) return;
    size_t n = 0;
    // Skip favorite markers / leading junk from PiDrive labels ("* Antenne…")
    while (*in == '*' || *in == (char)0xE2 /* utf8 lead of ★ often */ || *in == ' ' || *in == '\t') {
        // UTF-8 ★ = E2 98 85 — skip full sequence when present
        if ((unsigned char)in[0] == 0xE2 && (unsigned char)in[1] == 0x98 &&
            ((unsigned char)in[2] == 0x85 || (unsigned char)in[2] == 0x86)) {
            in += 3;
            continue;
        }
        if (*in == '*' || *in == ' ' || *in == '\t') {
            in++;
            continue;
        }
        break;
    }
    for (const unsigned char* p = (const unsigned char*)in; *p && n + 1 < outSz; ++p) {
        unsigned char c = *p;
        if (c < 0x20 || c == 0x7F) continue;
        if (c == '"' || c == '*' || c == '/' || c == ':' || c == '<' || c == '>' || c == '?' ||
            c == '\\' || c == '|') {
            continue;
        }
        // Replace fancy ellipsis … (E2 80 A6) with ASCII dots
        if (c == 0xE2 && p[1] == 0x80 && p[2] == 0xA6) {
            if (n + 3 < outSz) {
                out[n++] = '.';
                out[n++] = '.';
                out[n++] = '.';
            }
            p += 2;
            continue;
        }
        out[n++] = (char)c;
    }
    while (n > 0 && (out[n - 1] == ' ' || out[n - 1] == '.')) n--;
    out[n] = 0;
    if (n == 0) {
        strncpy(out, "Track", outSz - 1);
        out[outSz - 1] = 0;
    }
}

/** Map label to FAT 8.3 (name + "MP3"), uppercase ASCII. */
static void labelTo83(const char* label, uint8_t out[11]) {
    char clean[48];
    sanitizeFatLabel(label, clean, sizeof(clean));
    memset(out, ' ', 11);
    out[8] = 'M';
    out[9] = 'P';
    out[10] = '3';
    int n = 0;
    for (const char* p = clean; *p && n < 8; ++p) {
        unsigned char c = (unsigned char)*p;
        if (c >= 'a' && c <= 'z') c = (unsigned char)(c - 'a' + 'A');
        if (c == 0xC3) continue;  // UTF-8 lead — skip, next byte handled loosely
        if (c & 0x80) {
            if (c == 0xFC || c == 0xDC) c = 'U';
            else if (c == 0xE4 || c == 0xC4) c = 'A';
            else if (c == 0xF6 || c == 0xD6) c = 'O';
            else if (c == 0xDF) { /* ß */
                if (n < 7) {
                    out[n++] = 'S';
                    out[n++] = 'S';
                }
                continue;
            } else
                continue;
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
    char clean[64];
    sanitizeFatLabel(label, clean, sizeof(clean));
    int n = 0;
    const unsigned char* p = (const unsigned char*)clean;
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
    if (n == 0 && maxOut > 0) {
        const char* fb = "Track";
        while (*fb && n < maxOut) out[n++] = (uint16_t)*fb++;
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
    // STATIONS = cluster 2 → kStationsLba; SETTINGS = cluster 3 → kSettingsLba
    const bool stations = (lba == kStationsLba);
    const bool settings = (lba == kSettingsLba);
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
    if (lba >= kVirtSectorCount) return -1;
    uint8_t* out = (uint8_t*)buffer;
    if (bufsize > 4096) bufsize = 4096;  // TinyUSB practical cap
    // Base: demo image for low LBAs (stub MP3 payloads), else zeros.
    if (lba < DEMO_FAT_SECTOR_COUNT && offset < DEMO_FAT_SECTOR_SIZE) {
        uint32_t pos = lba * DEMO_FAT_SECTOR_SIZE + offset;
        uint32_t avail = DEMO_FAT_SIZE - pos;
        uint32_t n = bufsize;
        if (n > avail) n = avail;
        memcpy(out, DEMO_FAT_IMAGE + pos, n);
        if (n < bufsize) memset(out + n, 0, bufsize - n);
    } else {
        memset(out, 0, bufsize);
    }

    // Static view: BPB / FAT / ROOT / DIR / chains (identical whether streaming or not).
    if (offset == 0 && bufsize >= 512) {
        for (uint32_t off = 0; off + 512 <= bufsize; off += 512) {
            uint32_t sec = lba + off / 512;
            uint8_t* s = out + off;
            if (sec == 0) {
                patchBoot(s);
            } else if ((sec >= kFat0Lba && sec < kFat0Lba + kFatSpf) ||
                       (sec >= kFat1Lba && sec < kFat1Lba + kFatSpf)) {
                memset(s, 0, 512);
                if (sec == kFat0Lba || sec == kFat1Lba) {
                    s[0] = 0xF8;
                    s[1] = 0xFF;
                    s[2] = 0xFF;  // FAT12 media + EOC
                }
                patchFatFixed(s, sec);
            } else if (sec >= kRootLba0 && sec < kRootLba0 + kRootSectors) {
                patchRootDir(s, sec);
            } else {
                patchDirNames(s, sec);
                patchFatFixed(s, sec);  // no-op outside FAT LBAs
            }
        }
    }

    // File payload: live ringbuffer for active stream slot, else stub/pad.
    // Stub MP3 samples still live at legacy demo LBAs 43/59/75 inside demo_fat.bin.
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
            // During index: only ~1 KiB real stub — prevents HU caching a complete
            // demo song and playing it without further USB reads (no play.guess).
            // After msc.quiet, serve normal stub head until live overlay arms.
            const uint32_t stubSectors = indexSettled_ ? 16u : 2u;
            if (rel < stubSectors) {
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
    // Restore sticky MediaStore identity across OTG power-loss reboots (field 2026-09-30).
    loadRemountGenNvs();
    {
        char ser[12];
        formatPdSerial(remountGen_, ser, sizeof(ser));
        USB.serialNumber(ser);
        char rev[5];
        snprintf(rev, sizeof(rev), "%02u%02u", (unsigned)((remountGen_ / 100) % 100),
                 (unsigned)(remountGen_ % 100));
        MSC.productRevision(rev);
        if (events_) {
            char d[24];
            snprintf(d, sizeof(d), "nvs=%s", ser);
            events_->push("msc.identity", d);
        }
        Serial.printf("[MSC] identity from NVS gen=%u ser=%s\n", (unsigned)remountGen_, ser);
    }
    MSC.onStartStop(pidrive_msc_start_stop);
    MSC.onRead(pidrive_msc_read);
    MSC.onWrite(pidrive_msc_write);
    // Hold medium until menu_set / NVS present / timeout — avoids demo→menu flicker.
    mediaPresented_ = false;
    presentDeadlineMs_ = millis() + kMediaPresentTimeoutMs;
    MSC.mediaPresent(false);
    if (!MSC.begin(kVirtSectorCount, DEMO_FAT_SECTOR_SIZE)) {
        if (events_) events_->push("msc.fail", "begin");
        return false;
    }
    USB.begin();
    ready_ = true;
    HostScsiProbe::instance().begin(events_);
    if (events_) events_->push("msc.ready", "FAT12 virt 512k slots");
    Serial.printf("[MSC] ready virt_sectors=%u image=%u media=held timeout=%ums\n", kVirtSectorCount,
                  DEMO_FAT_SECTOR_COUNT, (unsigned)kMediaPresentTimeoutMs);
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
        bytesBoot_ = 0;
        bytesFat_ = 0;
        bytesDir_ = 0;
        seqBytes_ = 0;
        seqFile_ = nullptr;
        prefetchHits_ = 0;
        playRejectCount_ = 0;
        playGuessCount_ = 0;
        lastReadMs_ = 0;
        lastFileReadMs_ = 0;
        quietEmitted_ = false;
        indexSettled_ = false;
        phase_ = Phase::Idle;
        xfer512_ = xfer2k_ = xfer4k_ = xfer8kPlus_ = 0;
        memset(slotStats_, 0, sizeof(slotStats_));
        traceHead_ = 0;
        traceCount_ = 0;
        streamBytesServed_ = 0;
        plugCount_++;
        HostScsiProbe::instance().onPlug(plugMs_);
        if (events_) events_->push("usb.otg.up", "car-host");
        setPhase(Phase::Scan, "plug");
    } else {
        unplugCount_++;
        // Bump serial for the *next* attach so HU MediaStore cannot reuse PD0001 cache.
        // Persisted in NVS — survives ESP reboot when OTG unplug cuts board power.
        applyUsbIdentity();
        char ser[12];
        formatPdSerial(remountGen_, ser, sizeof(ser));
        if (events_) {
            char d[24];
            snprintf(d, sizeof(d), "next=%s", ser);
            events_->push("usb.otg.down", d);
        }
        if (menu_) menu_->clearPlaying();
        setPhase(Phase::Idle, "unplug");
        Serial.printf("[MSC] unplug — next serial %s\n", ser);
    }
}

void UsbMscGadget::onUsbSuspend(bool on) {
    if (!plugged_ || suspended_ == on) return;
    suspended_ = on;
    if (events_) events_->push(on ? "usb.otg.suspend" : "usb.otg.resume", "car-host");
}

void UsbMscGadget::onHostStartStop(uint8_t powerCondition, bool start, bool loadEject) {
    HostScsiProbe::instance().noteStartStop(powerCondition, start, loadEject);
}

void UsbMscGadget::noteXferSize(uint32_t bufsize) {
    if (bufsize <= 512)
        xfer512_++;
    else if (bufsize <= 2048)
        xfer2k_++;
    else if (bufsize <= 4096)
        xfer4k_++;
    else
        xfer8kPlus_++;
}

const char* UsbMscGadget::phaseName(Phase p) {
    switch (p) {
        case Phase::Scan:
            return "scan";
        case Phase::Index:
            return "index";
        case Phase::Play:
            return "play";
        case Phase::Quiet:
            return "quiet";
        default:
            return "idle";
    }
}

void UsbMscGadget::setPhase(Phase p, const char* detail) {
    if (phase_ == p) return;
    phase_ = p;
    char d[48];
    snprintf(d, sizeof(d), "%s%s%s", phaseName(p), (detail && detail[0]) ? " " : "",
             detail ? detail : "");
    if (events_) events_->push("msc.phase", d);
    emitDiag("msc.phase", d);
}

void UsbMscGadget::emitDiag(const char* code, const char* detail) {
    if (diagHandler_) diagHandler_(code, detail ? detail : "");
}

void UsbMscGadget::pushTrace(uint32_t lba, uint32_t bufsize, uint8_t kind, const char* tag) {
    MscReadSample& s = trace_[traceHead_];
    const uint32_t now = millis();
    s.ms = now;
    s.lba = lba;
    s.bytes = (uint16_t)(bufsize > 65535 ? 65535 : bufsize);
    s.gapMs = (lastReadMs_ && now >= lastReadMs_)
                  ? (uint16_t)((now - lastReadMs_) > 65535 ? 65535 : (now - lastReadMs_))
                  : 0;
    s.kind = kind;
    strncpy(s.tag, tag ? tag : "", sizeof(s.tag) - 1);
    s.tag[sizeof(s.tag) - 1] = 0;
    lastReadMs_ = now;
    traceHead_ = (traceHead_ + 1) % kTraceSize;
    if (traceCount_ < kTraceSize) traceCount_++;
}

UsbMscGadget::Region UsbMscGadget::classify(uint32_t lba, const MscFileMap** fileOut) const {
    if (fileOut) *fileOut = nullptr;
    if (lba == 0) return Region::Boot;
    if ((lba >= kFat0Lba && lba < kFat0Lba + kFatSpf) ||
        (lba >= kFat1Lba && lba < kFat1Lba + kFatSpf))
        return Region::Fat;
    if (isDirLba(lba)) return Region::Dir;
    if (lba < kDataStartLba) return Region::Meta;
    const MscFileMap* f = fileForLba(lba);
    if (f && f->active) {
        if (fileOut) *fileOut = f;
        return Region::File;
    }
    return Region::Meta;
}

int UsbMscGadget::slotIndex(const MscFileMap* f) const {
    if (!f) return -1;
    for (size_t i = 0; i < kSlots; i++) {
        if (&slots_[i] == f) return (int)i;
    }
    return -1;
}

UsbMscGadget::PlayEval UsbMscGadget::evaluatePlay(const MscFileMap* f, uint32_t startLba,
                                                  uint32_t seqBytes) const {
    if (!f || !f->active || !f->uid[0]) return PlayEval::BadFile;
    // Hosts often 4 KiB-align; first USB read may start a few LBAs after file start.
    if (startLba > f->lbaStart + playDetect_.headLbaSlop) return PlayEval::NotFromHead;
    // 512KiB slots: HU deep-indexes from head long after plugWindow — must not arm live
    // until the first quiet gap (index settled). Real play re-reads after that.
    if (!indexSettled_) return PlayEval::PlugWindow;
    if (playDetect_.plugWindowMs > 0 && plugMs_ &&
        (millis() - plugMs_) < playDetect_.plugWindowMs) {
        return PlayEval::PlugWindow;
    }
    if (seqBytes < minSeqFor(f)) return PlayEval::SeqShort;
    return PlayEval::Ok;
}

bool UsbMscGadget::isNavSlot(const MscFileMap* f) {
    if (!f || !f->active) return false;
    if (strncmp(f->uid, "pump:", 5) == 0) return true;
    if (strcmp(f->kind, "action") == 0 || strcmp(f->kind, "folder") == 0) return true;
    return false;
}

uint32_t UsbMscGadget::minSeqFor(const MscFileMap* f) const {
    uint32_t need = playDetect_.minSeqBytes;
    if (indexSettled_ && isNavSlot(f)) {
        need = playDetect_.navMinSeqBytes;
    }
    // Short page slot (~6 KiB): never demand more bytes than the file roughly holds.
    const uint32_t fbytes = slotBytes(*f);
    if (fbytes > 0 && fbytes < need + 1024u) {
        uint32_t shortNeed = (fbytes > 2048u) ? 2048u : fbytes;
        if (shortNeed < need) need = shortNeed;
    }
    return need;
}

const char* UsbMscGadget::playEvalName(PlayEval e) {
    switch (e) {
        case PlayEval::Ok:
            return "ok";
        case PlayEval::BadFile:
            return "bad_file";
        case PlayEval::NotFromHead:
            return "not_from_head";
        case PlayEval::PlugWindow:
            return "plug_window";
        case PlayEval::SeqShort:
            return "seq_short";
    }
    return "unknown";
}

void UsbMscGadget::emitPlayReject(PlayEval eval, const MscFileMap* f, uint32_t startLba,
                                  uint32_t seqBytes, const char* extra) {
    playRejectCount_++;
    const uint8_t code = (uint8_t)eval;
    // Treat "ok"+extra (cooldown/already) as distinct for rate-limit keying
    const uint8_t key = extra && extra[0] ? (uint8_t)(0x80 | (extra[0] & 0x7F)) : code;
    const bool reasonChanged = key != lastRejectEval_;
    if (!reasonChanged && millis() - lastRejectMs_ < 800) return;
    lastRejectMs_ = millis();
    lastRejectEval_ = key;

    const char* uid = (f && f->uid[0]) ? f->uid : "-";
    uint32_t age = (plugMs_) ? (millis() - plugMs_) : 0;
    const char* reason = (extra && extra[0] && eval == PlayEval::Ok) ? extra : playEvalName(eval);
    char d[96];
    if (extra && extra[0] && eval != PlayEval::Ok) {
        snprintf(d, sizeof(d), "%s uid=%s from=%u +%luB age=%lums %s", reason, uid,
                 (unsigned)startLba, (unsigned long)seqBytes, (unsigned long)age, extra);
    } else {
        snprintf(d, sizeof(d), "%s uid=%s from=%u +%luB age=%lums", reason, uid, (unsigned)startLba,
                 (unsigned long)seqBytes, (unsigned long)age);
    }
    Serial.printf("[MSC] play.reject %s\n", d);
    if (events_) events_->push("play.reject", d);
    emitDiag("play.reject", d);
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
    noteXferSize(bufsize);

    const MscFileMap* f = nullptr;
    Region reg = classify(lba, &f);
    const char* tag = "META";
    uint8_t kind = 0;
    if (reg == Region::Boot) {
        tag = "BOOT";
        kind = 4;
        bytesBoot_ += bufsize;
        bytesMeta_ += bufsize;
        if (phase_ == Phase::Idle || phase_ == Phase::Quiet) setPhase(Phase::Scan, "boot");
    } else if (reg == Region::Fat) {
        tag = metaTag(lba);
        kind = 5;
        bytesFat_ += bufsize;
        bytesMeta_ += bufsize;
        if (phase_ == Phase::Idle || phase_ == Phase::Quiet) setPhase(Phase::Scan, "fat");
    } else if (reg == Region::Dir) {
        tag = (lba < kDataStartLba) ? "ROOT" : "DIR";
        kind = 1;
        bytesDir_ += bufsize;
        bytesMeta_ += bufsize;
        if (phase_ != Phase::Play && phase_ != Phase::Index) setPhase(Phase::Scan, "dir");
    } else if (reg == Region::File && f) {
        tag = f->name;
        kind = 2;
        bytesFile_ += bufsize;
        lastFileReadMs_ = millis();
        quietEmitted_ = false;
        const int si = slotIndex(f);
        if (si >= 0) {
            slotStats_[si].bytes += bufsize;
            slotStats_[si].lastReadMs = lastFileReadMs_;
            if (lba <= f->lbaStart + playDetect_.headLbaSlop)
                slotStats_[si].fromHeadHits++;
            else
                slotStats_[si].midFileHits++;
        }
        // During plug window, file reads are indexing (not arming live).
        if (playDetect_.plugWindowMs > 0 && plugMs_ &&
            (millis() - plugMs_) < playDetect_.plugWindowMs) {
            if (phase_ != Phase::Play) setPhase(Phase::Index, f->name);
        }
    } else {
        tag = metaTag(lba);
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
            emitDiag("msc.first_read", d);
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
    {
        const int si = slotIndex(f);
        if (si >= 0 && seqBytes_ > slotStats_[si].maxSeqBytes)
            slotStats_[si].maxSeqBytes = seqBytes_;
    }

    PlayEval eval = evaluatePlay(f, seqStartLba_, seqBytes_);
    if (eval != PlayEval::Ok) {
        if (seqBytes_ >= 2048) {
            // Mid-file prefetch only — from-head reads must keep accumulating so a
            // phone that reads past the stub after the plug-window can still arm.
            if (seqStartLba_ > f->lbaStart + playDetect_.prefetchLbaSlop) {
                prefetchHits_++;
                emitPlayReject(eval, f, seqStartLba_, seqBytes_, "prefetch");
                if (events_ && millis() - lastEventMs_ > 800) {
                    lastEventMs_ = millis();
                    events_->push("msc.prefetch", f->uid);
                }
                return;
            }
            emitPlayReject(eval, f, seqStartLba_, seqBytes_);
        }
        return;
    }

    // One live-stream arm per cooldown window — neighboring *station* stubs are often
    // touched right after the first play and must not steal the stream.
    // Navigation (Zurueck / folder / Mehr) must still fire after auto-play (Feld 2026-09-29).
    if (playGuessMs_ != 0 && (millis() - playGuessMs_) < playDetect_.cooldownMs &&
        !isNavSlot(f)) {
        emitPlayReject(PlayEval::Ok, f, seqStartLba_, seqBytes_, "cooldown");
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
        playGuessCount_++;
        lastRejectEval_ = 0xFF;
        setPhase(Phase::Play, f->name);
        if (events_ && millis() - lastEventMs_ > 400) {
            lastEventMs_ = millis();
            events_->push("play.guess", f->uid);
            char d[56];
            snprintf(d, sizeof(d), "from=%u +%luB", (unsigned)seqStartLba_,
                     (unsigned long)seqBytes_);
            events_->push("msc.stream", d);
        }
        if (playHandler_) playHandler_(f->uid);
    } else {
        emitPlayReject(PlayEval::Ok, f, seqStartLba_, seqBytes_, "already_playing");
    }
}

void UsbMscGadget::loop() {
    if (!mediaPresented_ && presentDeadlineMs_ && (int32_t)(millis() - presentDeadlineMs_) >= 0) {
        presentMedia(remountHoldUntilMs_ ? "remount" : "timeout");
    }
    // Host finished indexing/reading stubs then went quiet → likely cached playback.
    // Critical BMW signal: Demo audible but no further MSC reads / no play.guess.
    if (plugged_ && !quietEmitted_ && lastFileReadMs_ && bytesFile_ >= 2048 &&
        phase_ != Phase::Play && (millis() - lastFileReadMs_) >= kQuietAfterFileMs) {
        quietEmitted_ = true;
        indexSettled_ = true;
        setPhase(Phase::Quiet, "cache?");
        if (events_) {
            char d[56];
            snprintf(d, sizeof(d), "file=%luB age=%lums", (unsigned long)bytesFile_,
                     (unsigned long)(millis() - lastFileReadMs_));
            events_->push("msc.quiet", d);
            emitDiag("msc.quiet", d);
        }
    }
}

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
        o["gap"] = s.gapMs;
        o["lba"] = s.lba;
        o["n"] = s.bytes;
        const char* kind = "meta";
        if (s.kind == 1)
            kind = "dir";
        else if (s.kind == 2)
            kind = "file";
        else if (s.kind == 3)
            kind = "wr";
        else if (s.kind == 4)
            kind = "boot";
        else if (s.kind == 5)
            kind = "fat";
        o["kind"] = kind;
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
    obj["bytesBoot"] = bytesBoot_;
    obj["bytesFat"] = bytesFat_;
    obj["bytesDir"] = bytesDir_;
    obj["bytesFile"] = bytesFile_;
    obj["prefetchHits"] = prefetchHits_;
    obj["playRejectCount"] = playRejectCount_;
    obj["playGuessCount"] = playGuessCount_;
    obj["streamSlot"] = streamSlot_;
    obj["streamBytes"] = streamBytesServed_;
    obj["fatMode"] = "static";
    obj["phase"] = phaseName(phase_);
    obj["mediaPresented"] = mediaPresented_;
    obj["remountGen"] = remountGen_;
    {
        char ser[12];
        formatPdSerial(remountGen_, ser, sizeof(ser));
        obj["usbSerial"] = ser;
    }
    obj["mediaWaitMs"] = (!mediaPresented_ && presentDeadlineMs_)
                             ? (int32_t)(presentDeadlineMs_ - millis())
                             : 0;
    {
        JsonObject pd = obj["playDetect"].to<JsonObject>();
        pd["plugWindowMs"] = playDetect_.plugWindowMs;
        pd["minSeqBytes"] = playDetect_.minSeqBytes;
        pd["navMinSeqBytes"] = playDetect_.navMinSeqBytes;
        pd["headLbaSlop"] = playDetect_.headLbaSlop;
        pd["cooldownMs"] = playDetect_.cooldownMs;
        pd["prefetchLbaSlop"] = playDetect_.prefetchLbaSlop;
    }
    {
        JsonObject x = obj["xfer"].to<JsonObject>();
        x["n512"] = xfer512_;
        x["n2k"] = xfer2k_;
        x["n4k"] = xfer4k_;
        x["n8kPlus"] = xfer8kPlus_;
    }
    {
        JsonObject host = obj["host"].to<JsonObject>();
        HostScsiProbe::instance().toJson(host);
    }
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
    obj["sectorCount"] = kVirtSectorCount;
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
        o["kind"] = slots_[i].kind;
        o["lba0"] = slots_[i].lbaStart;
        o["lba1"] = slots_[i].lbaEnd;
        o["active"] = slots_[i].active;
        o["bytes"] = slotStats_[i].bytes;
        o["fromHead"] = slotStats_[i].fromHeadHits;
        o["midFile"] = slotStats_[i].midFileHits;
        o["maxSeq"] = slotStats_[i].maxSeqBytes;
        o["lastAgeMs"] =
            slotStats_[i].lastReadMs ? (millis() - slotStats_[i].lastReadMs) : 0;
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
void UsbMscGadget::presentMedia(const char*) {}
void UsbMscGadget::remountMedia(const char*) {}
void UsbMscGadget::applyUsbIdentity() {}
void UsbMscGadget::loadRemountGenNvs() {}
void UsbMscGadget::saveRemountGenNvs() const {}
void UsbMscGadget::formatPdSerial(uint16_t, char* ser, size_t n) {
    if (ser && n) {
        ser[0] = '\0';
    }
}
void UsbMscGadget::startStream(const char*) {}
void UsbMscGadget::stopStream() {}
void UsbMscGadget::onUsbPlugged(bool) {}
void UsbMscGadget::onUsbSuspend(bool) {}
void UsbMscGadget::onHostStartStop(uint8_t, bool, bool) {}
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
void UsbMscGadget::noteXferSize(uint32_t) {}
void UsbMscGadget::setPhase(Phase, const char*) {}
void UsbMscGadget::emitDiag(const char*, const char*) {}
const char* UsbMscGadget::phaseName(Phase) { return "idle"; }
UsbMscGadget::Region UsbMscGadget::classify(uint32_t, const MscFileMap**) const { return Region::Meta; }
const MscFileMap* UsbMscGadget::fileForLba(uint32_t) const { return nullptr; }
int UsbMscGadget::slotIndex(const MscFileMap*) const { return -1; }
UsbMscGadget::PlayEval UsbMscGadget::evaluatePlay(const MscFileMap*, uint32_t, uint32_t) const {
    return PlayEval::BadFile;
}
bool UsbMscGadget::isNavSlot(const MscFileMap*) { return false; }
uint32_t UsbMscGadget::minSeqFor(const MscFileMap*) const { return 6000; }
const char* UsbMscGadget::playEvalName(PlayEval) { return "bad_file"; }
void UsbMscGadget::emitPlayReject(PlayEval, const MscFileMap*, uint32_t, uint32_t, const char*) {}
void UsbMscGadget::loadDefaultSlots() {}
void UsbMscGadget::patchBoot(uint8_t*) const {}
void UsbMscGadget::patchRootDir(uint8_t*, uint32_t) const {}
void UsbMscGadget::patchDirNames(uint8_t*, uint32_t) const {}
void UsbMscGadget::patchFatFixed(uint8_t*, uint32_t) const {}
void UsbMscGadget::patchFatChain(uint8_t*, uint32_t, uint16_t, uint16_t) const {}

#endif
