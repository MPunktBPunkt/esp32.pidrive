#include "Id3Store.h"
#include "StreamBuffer.h"
#include <SPIFFS.h>
#include <cstring>

uint32_t Id3Store::crc32(const uint8_t* p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int b = 0; b < 8; b++) {
            c = (c >> 1) ^ (0xEDB88320u & (uint32_t)-(int)(c & 1u));
        }
    }
    return ~c;
}

bool Id3Store::begin() {
    fsOk_ = SPIFFS.begin(true);
    if (!fsOk_) {
        Serial.println("[id3store] SPIFFS mount failed");
        return false;
    }
    bool ok = load();
    Serial.printf("[id3store] begin len=%u cover=%d\n", (unsigned)len_, hasCover() ? 1 : 0);
    return ok || len_ == 0;
}

bool Id3Store::set(const uint8_t* data, size_t n) {
    if (!data || n < 10 || n > kMax) return false;
    if (memcmp(data, "ID3", 3) != 0) return false;
    if (n == len_ && memcmp(buf_, data, n) == 0) {
        dirty_ = false;
        return true;
    }
    memcpy(buf_, data, n);
    if (n < kMax) memset(buf_ + n, 0, kMax - n);
    len_ = n;
    crc_ = crc32(buf_, len_);
    dirty_ = true;
    return true;
}

bool Id3Store::captureFromStream(StreamBuffer& stream) {
    size_t n = stream.id3Len();
    if (n < 10 || n > kMax) return false;
    // Copy into a side buffer then set() so identical tags stay non-dirty.
    // StreamBuffer::copyId3 is mux-safe; we reuse save path via temporary heap.
    uint8_t* tmp = static_cast<uint8_t*>(malloc(n));
    if (!tmp) return false;
    size_t got = stream.copyId3(tmp, n);
    bool ok = (got == n) && set(tmp, got);
    free(tmp);
    return ok;
}

size_t Id3Store::readAt(uint32_t fileOff, uint8_t* out, size_t n) const {
    if (!out || !n || fileOff >= len_) return 0;
    size_t avail = len_ - (size_t)fileOff;
    if (n > avail) n = avail;
    memcpy(out, buf_ + fileOff, n);
    return n;
}

bool Id3Store::load() {
    if (!fsOk_ && !(fsOk_ = SPIFFS.begin(true))) return false;
    if (!SPIFFS.exists(kPath)) {
        len_ = 0;
        dirty_ = false;
        return false;
    }
    File f = SPIFFS.open(kPath, "r");
    if (!f) return false;
    size_t n = f.size();
    if (n < 10 || n > kMax) {
        f.close();
        return false;
    }
    size_t got = f.read(buf_, n);
    f.close();
    if (got != n || memcmp(buf_, "ID3", 3) != 0) {
        len_ = 0;
        return false;
    }
    len_ = got;
    crc_ = crc32(buf_, len_);
    dirty_ = false;
    return true;
}

bool Id3Store::save() {
    if (!fsOk_ && !(fsOk_ = SPIFFS.begin(true))) return false;
    if (len_ < 10) return false;
    File f = SPIFFS.open(kPath, "w");
    if (!f) {
        Serial.println("[id3store] open write failed");
        return false;
    }
    size_t w = f.write(buf_, len_);
    f.close();
    if (w != len_) {
        Serial.printf("[id3store] write short %u/%u\n", (unsigned)w, (unsigned)len_);
        return false;
    }
    dirty_ = false;
    Serial.printf("[id3store] saved %u B crc=%08lx\n", (unsigned)len_, (unsigned long)crc_);
    return true;
}

bool Id3Store::saveIfDirty() {
    if (!dirty_ || len_ < 10) return false;
    return save();
}

void Id3Store::toJson(JsonObject obj) const {
    obj["len"] = (int)len_;
    obj["hasCover"] = hasCover();
    obj["dirty"] = dirty_;
    obj["crc"] = crc_;
    obj["path"] = kPath;
}
