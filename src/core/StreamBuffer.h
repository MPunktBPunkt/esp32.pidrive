#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <cstring>
#include "core/HuMem.h"

/**
 * Ring buffer for live MP3 + sticky ID3/APIC header at file start.
 *
 * Concurrency (0.4.54 / R13): push() runs on the Arduino loop (PUMP); readAt() /
 * liveAvail() / armCursorAtLiveHead() run on the TinyUSB task. All ring/cursor
 * mutations take mux_. Do not call WiFi/TCP from under this critical section.
 *
 * Storage (0.4.54): prefer PSRAM for the ring + ID3 (N8R2). Falls back to
 * internal heap. Capacity grows when PSRAM is present so Option-b Zeitversatz
 * and the Media-Leiter ~80 KiB head contract have headroom.
 */
class StreamBuffer {
public:
    /** DRAM fallback (pre-PSRAM contract ≈ 8.2 s @48 kbit/s). */
    static constexpr size_t kCapacityDram = 48 * 1024;
    /** PSRAM default: ~27 s @48 kbit/s; covers Head-160 (~80 KiB) + live window. */
    static constexpr size_t kCapacityPsram = 160 * 1024;
    static constexpr size_t kId3MaxDram = 12 * 1024;
    static constexpr size_t kId3MaxPsram = 16 * 1024;

    StreamBuffer() = default;
    ~StreamBuffer() { freeBuffers(); }

    /**
     * Allocate ring + ID3 (prefer PSRAM). Call once early in App::begin() before
     * MSC serves reads. Safe to call again (no-op if already allocated).
     */
    bool begin() { return ensureBuffers(); }

    size_t capacity() const { return capacity_; }
    size_t id3Max() const { return id3Max_; }
    bool buffersInPsram() const {
        return HuMem::ptrInPsram(data_) && HuMem::ptrInPsram(id3_);
    }

    void clear() {
        portENTER_CRITICAL(&mux_);
        head_ = 0;
        size_ = 0;
        absBase_ = 0;
        absEnd_ = 0;
        underruns_ = 0;
        active_ = false;
        uid_[0] = 0;
        id3Len_ = 0;
        headResyncs_ = 0;
        cursorArmed_ = false;
        hostAbsCursor_ = 0;
        hostExpectFileOff_ = 0;
        portEXIT_CRITICAL(&mux_);
    }

    void start(const char* uid) {
        clear();
        if (uid) {
            strncpy(uid_, uid, sizeof(uid_) - 1);
            uid_[sizeof(uid_) - 1] = 0;
        }
        portENTER_CRITICAL(&mux_);
        active_ = true;
        portEXIT_CRITICAL(&mux_);
    }

    void stop() {
        portENTER_CRITICAL(&mux_);
        active_ = false;
        portEXIT_CRITICAL(&mux_);
    }

    bool active() const {
        portENTER_CRITICAL(&mux_);
        bool a = active_;
        portEXIT_CRITICAL(&mux_);
        return a;
    }
    const char* uid() const { return uid_; }
    size_t size() const {
        portENTER_CRITICAL(&mux_);
        size_t s = size_;
        portEXIT_CRITICAL(&mux_);
        return s;
    }
    uint32_t absEnd() const {
        portENTER_CRITICAL(&mux_);
        uint32_t e = absEnd_;
        portEXIT_CRITICAL(&mux_);
        return e;
    }
    uint32_t underruns() const {
        portENTER_CRITICAL(&mux_);
        uint32_t u = underruns_;
        portEXIT_CRITICAL(&mux_);
        return u;
    }
    size_t freeSpace() const {
        portENTER_CRITICAL(&mux_);
        size_t f = capacity_ - size_;
        portEXIT_CRITICAL(&mux_);
        return f;
    }
    size_t id3Len() const {
        portENTER_CRITICAL(&mux_);
        size_t n = id3Len_;
        portEXIT_CRITICAL(&mux_);
        return n;
    }

    void clearId3() {
        portENTER_CRITICAL(&mux_);
        id3Len_ = 0;
        portEXIT_CRITICAL(&mux_);
    }

    /** Append to sticky ID3 header (file offset 0). */
    size_t appendId3(const uint8_t* data, size_t n) {
        if (!ensureBuffers() || !data || !n || !id3_) return 0;
        portENTER_CRITICAL(&mux_);
        size_t room = id3Max_ - id3Len_;
        if (n > room) n = room;
        if (n) {
            memcpy(id3_ + id3Len_, data, n);
            id3Len_ += n;
        }
        portEXIT_CRITICAL(&mux_);
        return n;
    }

    size_t push(const uint8_t* data, size_t n) {
        if (!ensureBuffers() || !data_ || !data || !n) return 0;
        size_t written = 0;
        portENTER_CRITICAL(&mux_);
        if (!active_) {
            portEXIT_CRITICAL(&mux_);
            return 0;
        }
        while (written < n) {
            if (size_ == capacity_) {
                head_ = (head_ + 1) % capacity_;
                size_--;
                absBase_++;
            }
            data_[(head_ + size_) % capacity_] = data[written++];
            size_++;
            absEnd_++;
        }
        portEXIT_CRITICAL(&mux_);
        return written;
    }

    /**
     * File layout: [sticky ID3 @0][audio via host cursor / abs window]
     * B3: underrun → CBR silence frames.
     * B4: sticky ID3 @0; head re-read snaps cursor to absBase_; sequential MSC
     *     reads continue from hostAbsCursor_ (survives ring scroll past head window).
     */
    size_t readAt(uint32_t fileOff, uint8_t* out, size_t n) {
        if (!out || !n) return 0;
        if (!ensureBuffers() || !data_ || !id3_) {
            memset(out, 0, n);
            return n;
        }
        static const uint8_t kSil[156] = {
            0xff, 0xfb, 0x30, 0x64, 0x00, 0x0f, 0xf0, 0x00, 0x00, 0x69, 0x00, 0x00, 0x00, 0x08,
            0x00, 0x00, 0x0d, 0x20, 0x00, 0x00, 0x01, 0x00, 0x00, 0x01, 0xa4, 0x00, 0x00, 0x00,
            0x20, 0x00, 0x00, 0x34, 0x80, 0x00, 0x00, 0x04, 0x4c, 0x41, 0x4d, 0x45, 0x33, 0x2e,
            0x31, 0x30, 0x30, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55,
            0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55,
            0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55,
            0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55,
            0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55,
            0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55,
            0x55, 0x55, 0x55, 0x55, 0x4c, 0x41, 0x4d, 0x45, 0x33, 0x2e, 0x31, 0x30, 0x30, 0x55,
            0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55,
            0x55, 0x55,
        };
        constexpr size_t kSilLen = sizeof(kSil);
        static constexpr uint32_t kHeadResyncFileBytes = 8192;
        static constexpr uint32_t kSeqSlop = 8192;

        portENTER_CRITICAL(&mux_);

        const size_t id3Len = id3Len_;
        const bool nearHead = fileOff < (uint32_t)id3Len + kHeadResyncFileBytes;
        const bool seekBack =
            (hostExpectFileOff_ != 0) && (fileOff + 512u < hostExpectFileOff_);
        const bool sequential =
            (hostExpectFileOff_ == 0) ||
            (fileOff + kSeqSlop >= hostExpectFileOff_ && fileOff <= hostExpectFileOff_ + kSeqSlop);

        bool useCursor = false;
        if (active_) {
            if (!cursorArmed_ || (nearHead && (seekBack || fileOff <= (uint32_t)id3Len + 512u))) {
                hostAbsCursor_ = absBase_;
                cursorArmed_ = true;
                useCursor = true;
                if (seekBack || fileOff <= (uint32_t)id3Len + 512u) headResyncs_++;
            } else if (sequential && cursorArmed_) {
                useCursor = true;
                if (size_ > 0 && hostAbsCursor_ < absBase_) hostAbsCursor_ = absBase_;
            }
        }

        for (size_t i = 0; i < n; i++) {
            uint32_t off = fileOff + (uint32_t)i;
            if (off < id3Len) {
                out[i] = id3_[off];
                continue;
            }
            uint32_t aoff;
            if (useCursor) {
                aoff = hostAbsCursor_;
            } else {
                aoff = off - (uint32_t)id3Len;
            }
            if (active_ && size_ > 0 && aoff >= absBase_ && aoff < absEnd_) {
                uint32_t rel = aoff - absBase_;
                out[i] = data_[(head_ + rel) % capacity_];
                if (useCursor) hostAbsCursor_++;
            } else {
                if (active_) underruns_++;
                out[i] = kSil[aoff % kSilLen];
                if (useCursor) hostAbsCursor_++;
            }
        }

        if (useCursor || sequential) {
            hostExpectFileOff_ = fileOff + (uint32_t)n;
        }

        portEXIT_CRITICAL(&mux_);
        return n;
    }

    size_t copyFrom(uint32_t absOff, uint8_t* out, size_t n) const {
        if (!out || !n || !data_) return 0;
        portENTER_CRITICAL(&mux_);
        if (!active_ || !size_) {
            portEXIT_CRITICAL(&mux_);
            return 0;
        }
        if (absOff < absBase_ || absOff >= absEnd_) {
            portEXIT_CRITICAL(&mux_);
            return 0;
        }
        size_t avail = (size_t)(absEnd_ - absOff);
        if (n > avail) n = avail;
        uint32_t rel = absOff - absBase_;
        for (size_t i = 0; i < n; i++) {
            out[i] = data_[(head_ + rel + i) % capacity_];
        }
        portEXIT_CRITICAL(&mux_);
        return n;
    }

    /** Copy sticky ID3 only (for Id3Store / zuletzt persist). */
    size_t copyId3(uint8_t* out, size_t n) const {
        if (!out || !n || !id3_) return 0;
        portENTER_CRITICAL(&mux_);
        size_t c = id3Len_ < n ? id3Len_ : n;
        if (c) memcpy(out, id3_, c);
        portEXIT_CRITICAL(&mux_);
        return c;
    }

    /** Lab probe: sticky ID3 + start of audio ring. */
    size_t copyId3AndAudio(uint8_t* out, size_t n) const {
        if (!out || !n || !id3_ || !data_) return 0;
        portENTER_CRITICAL(&mux_);
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
                out[got + i] = data_[(head_ + i) % capacity_];
            }
            got += c;
        }
        portEXIT_CRITICAL(&mux_);
        return got;
    }

    /**
     * Extract JPEG bytes from sticky ID3 APIC (mutagen v2.3/v2.4).
     * Returns copied length, or 0 if none.
     */
    size_t extractApicJpeg(uint8_t* out, size_t outCap) const {
        if (!out || !outCap || !id3_) return 0;
        portENTER_CRITICAL(&mux_);
        if (id3Len_ < 20) {
            portEXIT_CRITICAL(&mux_);
            return 0;
        }
        // Work on a snapshot of length; APIC parse stays under lock (ID3 ≤ 16 KiB).
        if (memcmp(id3_, "ID3", 3) != 0) {
            portEXIT_CRITICAL(&mux_);
            return 0;
        }
        const uint8_t major = id3_[3];
        auto synch = [](const uint8_t* p) -> uint32_t {
            return ((uint32_t)(p[0] & 0x7F) << 21) | ((uint32_t)(p[1] & 0x7F) << 14) |
                   ((uint32_t)(p[2] & 0x7F) << 7) | (uint32_t)(p[3] & 0x7F);
        };
        auto be32 = [](const uint8_t* p) -> uint32_t {
            return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
        };
        uint32_t tagSize = synch(id3_ + 6);
        size_t end = 10 + (size_t)tagSize;
        if (end > id3Len_) end = id3Len_;
        size_t pos = 10;
        if ((id3_[5] & 0x40) && pos + 4 <= end) {
            uint32_t ext = (major >= 4) ? synch(id3_ + pos) : be32(id3_ + pos);
            if (major >= 4) {
                if (ext < 6 || pos + ext > end) {
                    portEXIT_CRITICAL(&mux_);
                    return 0;
                }
                pos += ext;
            } else {
                if (pos + 4 + ext > end) {
                    portEXIT_CRITICAL(&mux_);
                    return 0;
                }
                pos += 4 + ext;
            }
        }
        size_t result = 0;
        while (pos + 10 <= end) {
            if (id3_[pos] == 0) break;
            const bool isApic = memcmp(id3_ + pos, "APIC", 4) == 0;
            uint32_t fsize = (major >= 4) ? synch(id3_ + pos + 4) : be32(id3_ + pos + 4);
            pos += 10;
            if (fsize == 0 || pos + fsize > end) break;
            if (isApic && fsize >= 10) {
                const uint8_t* p = id3_ + pos;
                size_t rem = fsize;
                uint8_t enc = p[0];
                p++;
                rem--;
                while (rem && *p) {
                    p++;
                    rem--;
                }
                if (rem) {
                    p++;
                    rem--;
                }
                if (!rem) break;
                p++;
                rem--;
                if (enc == 1 || enc == 2) {
                    while (rem >= 2 && (p[0] || p[1])) {
                        p += 2;
                        rem -= 2;
                    }
                    if (rem >= 2) {
                        p += 2;
                        rem -= 2;
                    }
                } else {
                    while (rem && *p) {
                        p++;
                        rem--;
                    }
                    if (rem) {
                        p++;
                        rem--;
                    }
                }
                if (!rem) break;
                const uint8_t* jpeg = p;
                size_t jlen = rem;
                for (size_t i = 0; i + 2 < rem; i++) {
                    if (p[i] == 0xFF && p[i + 1] == 0xD8 && p[i + 2] == 0xFF) {
                        jpeg = p + i;
                        jlen = rem - i;
                        break;
                    }
                }
                if (jlen > outCap) jlen = outCap;
                memcpy(out, jpeg, jlen);
                result = jlen;
                break;
            }
            pos += fsize;
        }
        portEXIT_CRITICAL(&mux_);
        return result;
    }

    uint32_t absBase() const {
        portENTER_CRITICAL(&mux_);
        uint32_t b = absBase_;
        portEXIT_CRITICAL(&mux_);
        return b;
    }
    uint32_t headResyncs() const {
        portENTER_CRITICAL(&mux_);
        uint32_t h = headResyncs_;
        portEXIT_CRITICAL(&mux_);
        return h;
    }
    uint32_t hostAbsCursor() const {
        portENTER_CRITICAL(&mux_);
        uint32_t c = hostAbsCursor_;
        portEXIT_CRITICAL(&mux_);
        return c;
    }
    bool cursorArmed() const {
        portENTER_CRITICAL(&mux_);
        bool a = cursorArmed_;
        portEXIT_CRITICAL(&mux_);
        return a;
    }

    /** Snap host cursor to absBase (head path) without copying bytes. */
    void armCursorAtLiveHead() {
        portENTER_CRITICAL(&mux_);
        hostAbsCursor_ = absBase_;
        cursorArmed_ = true;
        portEXIT_CRITICAL(&mux_);
    }

    /**
     * Live bytes ready at the host cursor (stall decision). 0 if inactive or cursor past absEnd.
     * Does not advance the cursor.
     */
    uint32_t liveAvail() const {
        portENTER_CRITICAL(&mux_);
        const bool act = active_;
        const bool armed = cursorArmed_;
        uint32_t cur = hostAbsCursor_;
        const uint32_t base = absBase_;
        const uint32_t end = absEnd_;
        portEXIT_CRITICAL(&mux_);
        if (!act || !armed) return 0;
        if (cur < base) cur = base;
        if (cur >= end) return 0;
        return end - cur;
    }

    /** Honest consumable audio at the MSC cursor; 48 kbit/s is the current CBR contract. */
    uint32_t liveAvailBytes() const { return liveAvail(); }
    uint32_t liveAvailMs() const {
        return (uint32_t)(((uint64_t)liveAvail() * 8u * 1000u) / 48000u);
    }

    void toJson(JsonObject obj) const {
        portENTER_CRITICAL(&mux_);
        obj["active"] = active_;
        obj["uid"] = uid_;
        obj["size"] = (int)size_;
        obj["cap"] = (int)capacity_;
        obj["absBase"] = absBase_;
        obj["absEnd"] = absEnd_;
        obj["underruns"] = underruns_;
        obj["id3Len"] = (int)id3Len_;
        obj["id3Max"] = (int)id3Max_;
        obj["headResyncs"] = headResyncs_;
        obj["cursorArmed"] = cursorArmed_;
        obj["hostAbsCursor"] = hostAbsCursor_;
        obj["hostExpectFileOff"] = hostExpectFileOff_;
        obj["psram"] = HuMem::ptrInPsram(data_);
        const bool hasCover = id3Len_ >= 20 && id3_ && id3_[0] == 'I' && id3_[1] == 'D' && id3_[2] == '3';
        const uint32_t liveB = (!active_ || !cursorArmed_)
                                   ? 0u
                                   : (hostAbsCursor_ < absBase_
                                          ? (absEnd_ > absBase_ ? absEnd_ - absBase_ : 0u)
                                          : (hostAbsCursor_ >= absEnd_ ? 0u : absEnd_ - hostAbsCursor_));
        portEXIT_CRITICAL(&mux_);
        obj["liveAvailBytes"] = liveB;
        obj["liveAvailMs"] = (uint32_t)(((uint64_t)liveB * 8u * 1000u) / 48000u);
        obj["hasCover"] = hasCover;
    }

private:
    bool ensureBuffers() {
        if (data_ && id3_ && capacity_ > 0) return true;
#ifdef BOARD_HAS_PSRAM
        const bool wantPsram = ESP.getPsramSize() > 0;
#else
        const bool wantPsram = false;
#endif
        capacity_ = wantPsram ? kCapacityPsram : kCapacityDram;
        id3Max_ = wantPsram ? kId3MaxPsram : kId3MaxDram;
        data_ = static_cast<uint8_t*>(HuMem::allocPreferPsram(capacity_));
        id3_ = static_cast<uint8_t*>(HuMem::allocPreferPsram(id3Max_));
        if (!data_ || !id3_) {
            freeBuffers();
            // Last resort: smaller DRAM ring so MSC still serves silence/ID3.
            capacity_ = kCapacityDram;
            id3Max_ = kId3MaxDram;
            data_ = static_cast<uint8_t*>(malloc(capacity_));
            id3_ = static_cast<uint8_t*>(malloc(id3Max_));
            if (!data_ || !id3_) {
                freeBuffers();
                return false;
            }
        }
        memset(data_, 0, capacity_);
        memset(id3_, 0, id3Max_);
        return true;
    }

    void freeBuffers() {
        HuMem::freePreferPsram(data_);
        HuMem::freePreferPsram(id3_);
        data_ = nullptr;
        id3_ = nullptr;
        capacity_ = 0;
        id3Max_ = 0;
    }

    uint8_t* data_ = nullptr;
    uint8_t* id3_ = nullptr;
    size_t capacity_ = 0;
    size_t id3Max_ = 0;
    size_t id3Len_ = 0;
    size_t head_ = 0;
    size_t size_ = 0;
    uint32_t absBase_ = 0;
    uint32_t absEnd_ = 0;
    uint32_t underruns_ = 0;
    uint32_t headResyncs_ = 0;
    bool cursorArmed_ = false;
    uint32_t hostAbsCursor_ = 0;
    uint32_t hostExpectFileOff_ = 0;
    bool active_ = false;
    char uid_[24] = {0};
    mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
};
