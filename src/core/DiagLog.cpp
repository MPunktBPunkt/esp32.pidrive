/**
 * @file DiagLog.cpp
 * @brief Serial + PSRAM ring + RTC tail diagnostic log (slim, no flash persist).
 */
#include "core/DiagLog.h"
#include "core/HuMem.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <esp_attr.h>

namespace {

constexpr size_t kRingCap = 64 * 1024;
constexpr size_t kRtcCap = 2048;
constexpr size_t kLineMax = 192;
constexpr uint32_t kRtcMagic = 0x50444447;  // PDDG

struct RtcRing {
    uint32_t magic;
    uint16_t next;
    uint16_t count;
    char data[kRtcCap];
};

RTC_NOINIT_ATTR RtcRing rtc;

char* ring_ = nullptr;
size_t ringNext_ = 0;
size_t ringCount_ = 0;
portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;

char prevTail_[kRtcCap + 1];
bool prevTailValid_ = false;

void rtcAppend(const char* line, size_t n) {
    if (!n) return;
    if (rtc.magic != kRtcMagic) {
        memset(&rtc, 0, sizeof(rtc));
        rtc.magic = kRtcMagic;
    }
    for (size_t i = 0; i < n; i++) {
        rtc.data[rtc.next % kRtcCap] = line[i];
        rtc.next = (rtc.next + 1) % kRtcCap;
        if (rtc.count < kRtcCap) rtc.count++;
    }
}

void ringAppend(const char* line, size_t n) {
    if (!ring_ || !n) return;
    for (size_t i = 0; i < n; i++) {
        ring_[ringNext_ % kRingCap] = line[i];
        ringNext_ = (ringNext_ + 1) % kRingCap;
        if (ringCount_ < kRingCap) ringCount_++;
    }
}

}  // namespace

namespace DiagLog {

void begin() {
    prevTailValid_ = false;
    prevTail_[0] = 0;
    if (rtc.magic == kRtcMagic && rtc.count <= kRtcCap && rtc.count > 0) {
        const uint16_t first = (rtc.next + kRtcCap - rtc.count) % kRtcCap;
        size_t out = 0;
        // Skip partial first line if the ring wrapped mid-line.
        bool skip = rtc.count == kRtcCap;
        for (uint16_t i = 0; i < rtc.count && out + 1 < sizeof(prevTail_); i++) {
            char c = rtc.data[(first + i) % kRtcCap];
            if (skip) {
                if (c == '\n') skip = false;
                continue;
            }
            prevTail_[out++] = c;
        }
        prevTail_[out] = 0;
        prevTailValid_ = out > 0;
    }

    memset(&rtc, 0, sizeof(rtc));
    rtc.magic = kRtcMagic;

    ring_ = static_cast<char*>(HuMem::allocPreferPsram(kRingCap));
    if (ring_ && !HuMem::ptrInPsram(ring_)) {
        // Keep only if it landed in PSRAM — otherwise free DRAM pressure.
        HuMem::freePreferPsram(ring_);
        ring_ = nullptr;
    }
    ringNext_ = 0;
    ringCount_ = 0;

    printf("[DIAG] begin prev_tail=%u ring_psram=%d\n", (unsigned)(prevTailValid_ ? strlen(prevTail_) : 0),
           ring_ ? 1 : 0);
}

void printf(const char* fmt, ...) {
    char body[kLineMax];
    va_list ap;
    va_start(ap, fmt);
    int bn = vsnprintf(body, sizeof(body), fmt, ap);
    va_end(ap);
    if (bn < 0) return;
    if (static_cast<size_t>(bn) >= sizeof(body)) bn = (int)sizeof(body) - 1;

    Serial.write(reinterpret_cast<const uint8_t*>(body), (size_t)bn);
    if (bn == 0 || body[bn - 1] != '\n') Serial.write('\n');

    char line[kLineMax + 24];
    int ln = snprintf(line, sizeof(line), "%lu %s", (unsigned long)millis(), body);
    if (ln < 0) return;
    if (static_cast<size_t>(ln) >= sizeof(line)) ln = (int)sizeof(line) - 1;
    if (ln == 0 || line[ln - 1] != '\n') {
        if (static_cast<size_t>(ln) + 1 < sizeof(line)) {
            line[ln++] = '\n';
            line[ln] = 0;
        }
    }

    portENTER_CRITICAL(&mux_);
    rtcAppend(line, (size_t)ln);
    ringAppend(line, (size_t)ln);
    portEXIT_CRITICAL(&mux_);
}

const char* previousTail() { return prevTailValid_ ? prevTail_ : ""; }

size_t size() {
    portENTER_CRITICAL(&mux_);
    size_t n = ringCount_;
    portEXIT_CRITICAL(&mux_);
    return n;
}

size_t copyOut(size_t offset, char* dst, size_t n) {
    if (!dst || !n || !ring_) return 0;
    portENTER_CRITICAL(&mux_);
    if (offset >= ringCount_) {
        portEXIT_CRITICAL(&mux_);
        return 0;
    }
    size_t avail = ringCount_ - offset;
    if (n > avail) n = avail;
    const size_t first = (ringNext_ + kRingCap - ringCount_) % kRingCap;
    for (size_t i = 0; i < n; i++) {
        dst[i] = ring_[(first + offset + i) % kRingCap];
    }
    portEXIT_CRITICAL(&mux_);
    return n;
}

void appendStatus(JsonObject obj) {
    JsonObject d = obj["diag"].to<JsonObject>();
    d["ring_bytes"] = (int)size();
    d["prev_tail_bytes"] = (int)(prevTailValid_ ? strlen(prevTail_) : 0);
    d["ring_psram"] = ring_ != nullptr;
}

void appendPreviousTail(JsonObject obj) {
    if (!prevTailValid_ || !prevTail_[0]) return;
    obj["diag_tail"] = prevTail_;
}

}  // namespace DiagLog
