#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <cstddef>
#include <cstdint>

/**
 * Last sticky ID3/APIC for 0ZULETZT.MP3 across reboot.
 *
 * Live overlay still uses StreamBuffer; this store is the resume head for the
 * zuletzt slot when the stream is idle (boot / after audio_stop).
 * Persistence: SPIFFS /zuletzt.id3 (NVS too small for ≤8 KiB APIC tags).
 */
class Id3Store {
public:
    static constexpr size_t kMax = 8 * 1024;
    static constexpr const char* kPath = "/zuletzt.id3";

    bool begin();
    /** Replace RAM copy; dirty until save() / saveIfDirty(). */
    bool set(const uint8_t* data, size_t n);
    /** Copy sticky ID3 out of the live stream (no large stack temp). */
    bool captureFromStream(class StreamBuffer& stream);

    size_t len() const { return len_; }
    bool hasCover() const {
        return len_ >= 20 && buf_[0] == 'I' && buf_[1] == 'D' && buf_[2] == '3';
    }
    const uint8_t* data() const { return buf_; }

    /** File-offset read for MSC: ID3 bytes, or 0 if past end / empty. */
    size_t readAt(uint32_t fileOff, uint8_t* out, size_t n) const;

    bool load();
    bool save();
    /** Save when dirty; returns true if a write happened. */
    bool saveIfDirty();
    void markDirty() { dirty_ = true; }
    bool dirty() const { return dirty_; }

    void toJson(JsonObject obj) const;

private:
    uint8_t buf_[kMax] = {0};
    size_t len_ = 0;
    bool dirty_ = false;
    bool fsOk_ = false;
    uint32_t crc_ = 0;

    static uint32_t crc32(const uint8_t* p, size_t n);
};
