#include "MessCapture.h"
#include <cstring>

#if !ARDUINO_USB_MODE
#include "tusb.h"
#endif

MessCapture& MessCapture::instance() {
    static MessCapture m;
    return m;
}

void MessCapture::setEnabled(bool on) {
    enabled_ = on;
    if (!on) {
        portENTER_CRITICAL(&mux_);
        active_ = false;
        portEXIT_CRITICAL(&mux_);
    }
}

bool MessCapture::beginCapture(MessCaptureMode mode) {
    if (mode != MessCaptureMode::RamLocal && mode != MessCaptureMode::RamWifiOff) return false;
    portENTER_CRITICAL(&mux_);
    if (captureActive_) {
        portEXIT_CRITICAL(&mux_);
        return false;
    }
    if (active_) flushActive(1);
    head_ = 0;
    count_ = 0;
    captureMode_ = mode;
    captureActive_ = true;
    captureComplete_ = false;
    captureStartSeq_ = nextSeq_;
    captureEndSeq_ = 0;
    captureStartDrops_ = dropCount_;
    captureEndDrops_ = dropCount_;
    captureEndReason_[0] = 0;
    emitSeq_ = nextSeq_ ? nextSeq_ - 1 : 0;
    portEXIT_CRITICAL(&mux_);
    return true;
}

void MessCapture::endCapture(const char* reason) {
    portENTER_CRITICAL(&mux_);
    if (!captureActive_) {
        portEXIT_CRITICAL(&mux_);
        return;
    }
    if (active_) flushActive(1);
    captureActive_ = false;
    captureComplete_ = true;
    captureEndSeq_ = nextSeq_ ? nextSeq_ - 1 : 0;
    captureEndDrops_ = dropCount_;
    emitSeq_ = captureEndSeq_;  // explicit API replay only; never flood PUMP after capture
    const char* r = (reason && reason[0]) ? reason : "stopped";
    strncpy(captureEndReason_, r, sizeof(captureEndReason_) - 1);
    captureEndReason_[sizeof(captureEndReason_) - 1] = 0;
    portEXIT_CRITICAL(&mux_);
}

const char* MessCapture::captureModeName() const {
    switch (captureMode_) {
        case MessCaptureMode::RamLocal: return "ram_local";
        case MessCaptureMode::RamWifiOff: return "ram_wifi_off";
        default: return "none";
    }
}

void MessCapture::reset() {
    portENTER_CRITICAL(&mux_);
    active_ = false;
    head_ = 0;
    count_ = 0;
    cmdCount_ = 0;
    dropCount_ = 0;
    chunkCount_ = 0;
    lastCompleteUs_ = 0;
    clockLastRawUs_ = 0;
    clockEpochUs_ = 0;
    nextSeq_ = 1;
    emitSeq_ = 0;
    emitDropCount_ = 0;
    captureActive_ = false;
    captureComplete_ = false;
    captureMode_ = MessCaptureMode::None;
    captureStartSeq_ = 0;
    captureEndSeq_ = 0;
    captureStartDrops_ = 0;
    captureEndDrops_ = 0;
    captureEndReason_[0] = 0;
    memset(&cur_, 0, sizeof(cur_));
    portEXIT_CRITICAL(&mux_);
}

void MessCapture::pushCmd(const MessCmd& c) {
    MessCmd stored = c;
    stored.seq = nextSeq_++;
    if (count_ == kRing) {
        head_ = (head_ + 1) % kRing;
        count_--;
        dropCount_++;
    }
    size_t i = (head_ + count_) % kRing;
    ring_[i] = stored;
    count_++;
    cmdCount_++;
}

ProbeClock MessCapture::nowUsLocked() {
    const uint32_t raw = micros();
    if (raw < clockLastRawUs_) clockEpochUs_ += (1ULL << 32);
    clockLastRawUs_ = raw;
    return (ProbeClock)(clockEpochUs_ + raw);
}

void MessCapture::flushActive(uint8_t status) {
    if (!active_) return;
    cur_.status = status;
    cur_.t1Us = nowUsLocked();
    pushCmd(cur_);
    lastCompleteUs_ = cur_.t1Us;
    active_ = false;
    memset(&cur_, 0, sizeof(cur_));
}

void MessCapture::mark(const char* tag) {
    MessCmd m;
    m.op = 0xFF;
    m.status = 2;
    m.source = MessSource::Marker;
    if (tag && tag[0]) {
        strncpy(m.tag, tag, sizeof(m.tag) - 1);
        m.tag[sizeof(m.tag) - 1] = 0;
    }
    portENTER_CRITICAL(&mux_);
    m.t0Us = m.t1Us = nowUsLocked();
    pushCmd(m);
    portEXIT_CRITICAL(&mux_);
}

bool MessCapture::usbDetach(uint16_t holdMs) {
#if !ARDUINO_USB_MODE
    bool ok = tud_disconnect();
    if (ok) {
        usbAttached_ = false;
        usbDetachCount_++;
        usbReattachAtMs_ = holdMs ? (millis() + holdMs) : 0;
    }
    return ok;
#else
    (void)holdMs;
    return false;
#endif
}

bool MessCapture::usbAttach() {
#if !ARDUINO_USB_MODE
    bool ok = tud_connect();
    if (ok) {
        usbAttached_ = true;
        usbAttachCount_++;
        usbReattachAtMs_ = 0;
    }
    return ok;
#else
    return false;
#endif
}

void MessCapture::noteRead10Chunk(uint32_t lba, uint32_t offset, uint32_t bufsize, int32_t ret) {
    if (!enabled_) return;
    portENTER_CRITICAL(&mux_);
    const ProbeClock now = nowUsLocked();
    chunkCount_++;
    if (!active_) {
        cur_ = MessCmd{};
        cur_.t0Us = now;
        cur_.gapUs = lastCompleteUs_ ? (now - lastCompleteUs_) : 0;
        cur_.lba = lba;
        cur_.op = 0x28;
        cur_.source = MessSource::Synthesized;
        cur_.synthesized = true;  // command reconstructed from TinyUSB chunk callbacks
        active_ = true;
    }
    cur_.chunks++;
    if (ret == 0) {
        cur_.busyN++;
    } else if (ret > 0) {
        cur_.bytes += (uint32_t)ret;
        cur_.t1Us = now;
        (void)offset;
        (void)bufsize;
    }
    portEXIT_CRITICAL(&mux_);
}

void MessCapture::noteRead10Complete(uint8_t lun) {
    (void)lun;
    if (!enabled_) return;
    portENTER_CRITICAL(&mux_);
    if (active_ && cur_.op == 0x28) {
        flushActive(0);
    }
    portEXIT_CRITICAL(&mux_);
}

void MessCapture::noteWrite10Chunk(uint32_t lba, uint32_t offset, uint32_t bufsize, int32_t ret) {
    if (!enabled_) return;
    portENTER_CRITICAL(&mux_);
    const ProbeClock now = nowUsLocked();
    chunkCount_++;
    if (!active_) {
        cur_ = MessCmd{};
        cur_.t0Us = now;
        cur_.gapUs = lastCompleteUs_ ? (now - lastCompleteUs_) : 0;
        cur_.lba = lba;
        cur_.op = 0x2A;
        cur_.source = MessSource::Synthesized;
        cur_.synthesized = true;
        active_ = true;
    }
    cur_.chunks++;
    if (ret == 0) cur_.busyN++;
    else if (ret > 0) {
        cur_.bytes += (uint32_t)ret;
        cur_.t1Us = now;
    }
    (void)offset;
    (void)bufsize;
    portEXIT_CRITICAL(&mux_);
}

void MessCapture::noteWrite10Complete(uint8_t lun) {
    (void)lun;
    if (!enabled_) return;
    portENTER_CRITICAL(&mux_);
    if (active_ && cur_.op == 0x2A) {
        flushActive(0);
    }
    portEXIT_CRITICAL(&mux_);
}

void MessCapture::loop() {
#if !ARDUINO_USB_MODE
    if (usbReattachAtMs_ && !usbAttached_ && (int32_t)(millis() - usbReattachAtMs_) >= 0) {
        usbAttach();
    }
#endif
    if (captureActive_ || !emit_) return;
    MessCmd local[16];
    size_t n = 0;
    portENTER_CRITICAL(&mux_);
    if (count_) {
        const uint64_t oldest = ring_[head_].seq;
        if (emitSeq_ && emitSeq_ + 1 < oldest) {
            emitDropCount_ += (uint32_t)(oldest - emitSeq_ - 1);
            emitSeq_ = oldest - 1;
        }
        for (size_t i = 0; i < count_ && n < 16; i++) {
            const MessCmd& c = ring_[(head_ + i) % kRing];
            if (c.seq > emitSeq_) local[n++] = c;
        }
        if (n) emitSeq_ = local[n - 1].seq;
    }
    portEXIT_CRITICAL(&mux_);
    for (size_t i = 0; i < n; i++) emit_(local[i]);
}

void MessCapture::toJson(JsonObject obj) const {
    size_t count;
    bool active;
    bool captureActive;
    bool captureComplete;
    MessCaptureMode captureMode;
    uint64_t captureStartSeq;
    uint64_t captureEndSeq;
    uint32_t captureStartDrops;
    uint32_t captureEndDrops;
    char captureEndReason[sizeof(captureEndReason_)];
    uint64_t oldestSeq;
    uint64_t newest;
    uint64_t emitSeq;
    uint32_t cmds;
    uint32_t drops;
    uint32_t chunks;
    uint32_t emitDrops;
    portENTER_CRITICAL(&mux_);
    count = count_;
    active = active_;
    captureActive = captureActive_;
    captureComplete = captureComplete_;
    captureMode = captureMode_;
    captureStartSeq = captureStartSeq_;
    captureEndSeq = captureEndSeq_;
    captureStartDrops = captureStartDrops_;
    captureEndDrops = captureEndDrops_;
    memcpy(captureEndReason, captureEndReason_, sizeof(captureEndReason));
    oldestSeq = count_ ? ring_[head_].seq : 0;
    newest = nextSeq_ ? nextSeq_ - 1 : 0;
    emitSeq = emitSeq_;
    cmds = cmdCount_;
    drops = dropCount_;
    chunks = chunkCount_;
    emitDrops = emitDropCount_;
    portEXIT_CRITICAL(&mux_);

    obj["enabled"] = enabled_;
    obj["usbAttached"] = usbAttached_;
    obj["usbDetachN"] = usbDetachCount_;
    obj["usbAttachN"] = usbAttachCount_;
    obj["cmds"] = cmds;
    obj["drops"] = drops;
    obj["chunks"] = chunks;
    obj["ring"] = (uint32_t)count;
    obj["capacity"] = (uint32_t)kRing;
    obj["mode"] = captureMode == MessCaptureMode::RamLocal
                      ? "ram_local"
                      : (captureMode == MessCaptureMode::RamWifiOff ? "ram_wifi_off" : "none");
    obj["captureActive"] = captureActive;
    obj["startSeq"] = captureStartSeq;
    const uint64_t reportedEndSeq = captureActive ? newest : captureEndSeq;
    const uint32_t captureOverflow =
        captureActive ? drops - captureStartDrops : captureEndDrops - captureStartDrops;
    obj["endSeq"] = reportedEndSeq;
    obj["overflow"] = captureOverflow;
    obj["drop"] = drops;
    obj["complete"] = captureComplete && captureOverflow == 0;
    obj["endReason"] = captureEndReason;
    obj["oldestSeq"] = oldestSeq;
    obj["newestSeq"] = newest;
    obj["emitSeq"] = emitSeq;
    obj["emitDrops"] = emitDrops;
    obj["activeCmd"] = active;
    obj["healthy"] = captureMode == MessCaptureMode::None
                         ? (drops == 0 && emitDrops == 0)
                         : (captureOverflow == 0);
    obj["clock"] = "probe-int64-us";
    obj["port"] = "esp-capture-port";
    obj["controlOwner"] = "app";
    obj["vbusNote"] = "soft_usb=D+_pullup_not_VBUS";
#if defined(PIDRIVE_MESS) && PIDRIVE_MESS
    obj["build"] = "mess";
#else
    obj["build"] = "std";
#endif
}

size_t MessCapture::copyRecent(MessCmd* out, size_t maxCmds) const {
    if (!out || !maxCmds) return 0;
    portENTER_CRITICAL(&mux_);
    size_t n = count_ < maxCmds ? count_ : maxCmds;
    size_t start = (head_ + (count_ - n)) % kRing;
    for (size_t i = 0; i < n; i++) {
        out[i] = ring_[(start + i) % kRing];
    }
    portEXIT_CRITICAL(&mux_);
    return n;
}

size_t MessCapture::copyAfter(uint64_t afterSeq, MessCmd* out, size_t maxCmds) const {
    if (!out || !maxCmds) return 0;
    portENTER_CRITICAL(&mux_);
    size_t n = 0;
    for (size_t i = 0; i < count_ && n < maxCmds; i++) {
        const MessCmd& c = ring_[(head_ + i) % kRing];
        if (c.seq > afterSeq) out[n++] = c;
    }
    portEXIT_CRITICAL(&mux_);
    return n;
}

#if defined(PIDRIVE_MESS) && PIDRIVE_MESS && !ARDUINO_USB_MODE

extern "C" {
int32_t __real_tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void* buffer,
                                 uint32_t bufsize);
int32_t __real_tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t* buffer,
                                  uint32_t bufsize);
}

extern "C" int32_t __wrap_tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void* buffer,
                                            uint32_t bufsize) {
    int32_t r = __real_tud_msc_read10_cb(lun, lba, offset, buffer, bufsize);
    MessCapture::instance().noteRead10Chunk(lba, offset, bufsize, r);
    return r;
}

extern "C" int32_t __wrap_tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset,
                                             uint8_t* buffer, uint32_t bufsize) {
    int32_t r = __real_tud_msc_write10_cb(lun, lba, offset, buffer, bufsize);
    MessCapture::instance().noteWrite10Chunk(lba, offset, bufsize, r);
    return r;
}

extern "C" void tud_msc_read10_complete_cb(uint8_t lun) {
    MessCapture::instance().noteRead10Complete(lun);
}

extern "C" void tud_msc_write10_complete_cb(uint8_t lun) {
    MessCapture::instance().noteWrite10Complete(lun);
}

#endif
