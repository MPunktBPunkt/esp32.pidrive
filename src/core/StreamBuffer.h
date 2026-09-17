#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <cstring>

/** Ring buffer for live MP3 bytes, addressable by absolute file offset. */
class StreamBuffer {
public:
    static constexpr size_t kCapacity = 48 * 1024;

    void clear() {
        head_ = 0;
        size_ = 0;
        absBase_ = 0;
        absEnd_ = 0;
        underruns_ = 0;
        active_ = false;
        uid_[0] = 0;
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

    /** Append MP3 bytes (newest). Drops oldest on overflow. */
    size_t push(const uint8_t* data, size_t n) {
        if (!active_ || !data || !n) return 0;
        size_t written = 0;
        while (written < n) {
            if (size_ == kCapacity) {
                // drop one byte from oldest
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

    /**
     * Copy bytes for absolute file offset into out.
     * Offsets before absBase_ → silence (0xFF padding for MP3 resync friendliness).
     * Past absEnd_ → underrun fill 0x00.
     */
    size_t readAt(uint32_t fileOff, uint8_t* out, size_t n) {
        if (!out || !n) return 0;
        for (size_t i = 0; i < n; i++) {
            uint32_t off = fileOff + (uint32_t)i;
            if (!active_ || size_ == 0 || off < absBase_) {
                out[i] = 0xFF;
                if (active_ && off >= absBase_) underruns_++;
            } else if (off >= absEnd_) {
                out[i] = 0x00;
                underruns_++;
            } else {
                uint32_t rel = off - absBase_;
                out[i] = data_[(head_ + rel) % kCapacity];
            }
        }
        return n;
    }

    /** Copy up to n bytes starting at absolute offset; 0 if not in buffer. */
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

    uint32_t absBase() const { return absBase_; }

    void toJson(JsonObject obj) const {
        obj["active"] = active_;
        obj["uid"] = uid_;
        obj["size"] = (int)size_;
        obj["cap"] = (int)kCapacity;
        obj["absEnd"] = absEnd_;
        obj["underruns"] = underruns_;
    }

private:
    uint8_t data_[kCapacity];
    size_t head_ = 0;
    size_t size_ = 0;
    uint32_t absBase_ = 0;
    uint32_t absEnd_ = 0;
    uint32_t underruns_ = 0;
    bool active_ = false;
    char uid_[24] = {0};
};
