#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <cstring>

/** Ring buffer for live MP3 + sticky ID3/APIC header at file start. */
class StreamBuffer {
public:
    static constexpr size_t kCapacity = 48 * 1024;
    static constexpr size_t kId3Max = 12 * 1024;

    void clear() {
        head_ = 0;
        size_ = 0;
        absBase_ = 0;
        absEnd_ = 0;
        underruns_ = 0;
        active_ = false;
        uid_[0] = 0;
        id3Len_ = 0;
    }

    void start(const char* uid) {
        clear();
        if (uid) {
            strncpy(uid_, uid, sizeof(uid_) - 1);
            uid_[sizeof(uid_) - 1] = 0;
        }
        active_ = true;
    }

    void stop() { active_ = false; }

    bool active() const { return active_; }
    const char* uid() const { return uid_; }
    size_t size() const { return size_; }
    uint32_t absEnd() const { return absEnd_; }
    uint32_t underruns() const { return underruns_; }
    size_t freeSpace() const { return kCapacity - size_; }
    size_t id3Len() const { return id3Len_; }

    void clearId3() { id3Len_ = 0; }

    /** Append to sticky ID3 header (file offset 0). */
    size_t appendId3(const uint8_t* data, size_t n) {
        if (!data || !n) return 0;
        size_t room = kId3Max - id3Len_;
        if (n > room) n = room;
        if (!n) return 0;
        memcpy(id3_ + id3Len_, data, n);
        id3Len_ += n;
        return n;
    }

    size_t push(const uint8_t* data, size_t n) {
        if (!active_ || !data || !n) return 0;
        size_t written = 0;
        while (written < n) {
            if (size_ == kCapacity) {
                head_ = (head_ + 1) % kCapacity;
                size_--;
                absBase_++;
            }
            data_[(head_ + size_) % kCapacity] = data[written++];
            size_++;
            absEnd_++;
        }
        return written;
    }

    /** File layout: [sticky ID3][audio abs 0..] */
    size_t readAt(uint32_t fileOff, uint8_t* out, size_t n) {
        if (!out || !n) return 0;
        for (size_t i = 0; i < n; i++) {
            uint32_t off = fileOff + (uint32_t)i;
            if (off < id3Len_) {
                out[i] = id3_[off];
                continue;
            }
            uint32_t aoff = off - (uint32_t)id3Len_;
            if (!active_ || size_ == 0 || aoff < absBase_) {
                out[i] = 0xFF;
                if (active_ && aoff >= absBase_) underruns_++;
            } else if (aoff >= absEnd_) {
                out[i] = 0x00;
                underruns_++;
            } else {
                uint32_t rel = aoff - absBase_;
                out[i] = data_[(head_ + rel) % kCapacity];
            }
        }
        return n;
    }

    size_t copyFrom(uint32_t absOff, uint8_t* out, size_t n) const {
        if (!out || !n || !active_ || !size_) return 0;
        if (absOff < absBase_ || absOff >= absEnd_) return 0;
        size_t avail = (size_t)(absEnd_ - absOff);
        if (n > avail) n = avail;
        uint32_t rel = absOff - absBase_;
        for (size_t i = 0; i < n; i++) {
            out[i] = data_[(head_ + rel + i) % kCapacity];
        }
        return n;
    }

    /** Lab probe: sticky ID3 + start of audio ring. */
    size_t copyId3AndAudio(uint8_t* out, size_t n) const {
        if (!out || !n) return 0;
        size_t got = 0;
        if (id3Len_) {
            size_t c = id3Len_ < n ? id3Len_ : n;
            memcpy(out, id3_, c);
            got = c;
        }
        if (got < n && size_) {
            size_t need = n - got;
            size_t c = size_ < need ? size_ : need;
            for (size_t i = 0; i < c; i++) {
                out[got + i] = data_[(head_ + i) % kCapacity];
            }
            got += c;
        }
        return got;
    }

    uint32_t absBase() const { return absBase_; }

    void toJson(JsonObject obj) const {
        obj["active"] = active_;
        obj["uid"] = uid_;
        obj["size"] = (int)size_;
        obj["cap"] = (int)kCapacity;
        obj["absEnd"] = absEnd_;
        obj["underruns"] = underruns_;
        obj["id3Len"] = (int)id3Len_;
    }

private:
    uint8_t data_[kCapacity];
    uint8_t id3_[kId3Max];
    size_t id3Len_ = 0;
    size_t head_ = 0;
    size_t size_ = 0;
    uint32_t absBase_ = 0;
    uint32_t absEnd_ = 0;
    uint32_t underruns_ = 0;
    bool active_ = false;
    char uid_[24] = {0};
};
