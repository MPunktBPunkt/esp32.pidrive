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

void MessCapture::reset() {
    portENTER_CRITICAL(&mux_);
    active_ = false;
    head_ = 0;
    count_ = 0;
    cmdCount_ = 0;
    dropCount_ = 0;
    chunkCount_ = 0;
    lastCompleteUs_ = 0;
    pendingEmitN_ = 0;
    memset(&cur_, 0, sizeof(cur_));
    portEXIT_CRITICAL(&mux_);
}

void MessCapture::pushCmd(const MessCmd& c) {
    if (count_ == kRing) {
        head_ = (head_ + 1) % kRing;
        count_--;
        dropCount_++;
    }
    size_t i = (head_ + count_) % kRing;
    ring_[i] = c;
    count_++;
    cmdCount_++;
    if (pendingEmitN_ < (sizeof(pendingEmit_) / sizeof(pendingEmit_[0]))) {
        pendingEmit_[pendingEmitN_++] = c;
    }
}

void MessCapture::flushActive(uint8_t status) {
    if (!active_) return;
    cur_.status = status;
    cur_.t1Us = micros();
    pushCmd(cur_);
    lastCompleteUs_ = cur_.t1Us;
    active_ = false;
    memset(&cur_, 0, sizeof(cur_));
}

void MessCapture::mark(const char* tag) {
    MessCmd m;
    m.t0Us = m.t1Us = micros();
    m.op = 0xFF;
    m.status = 2;
    if (tag && tag[0]) {
        strncpy(m.tag, tag, sizeof(m.tag) - 1);
        m.tag[sizeof(m.tag) - 1] = 0;
    }
    portENTER_CRITICAL(&mux_);
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
    const uint32_t now = micros();
    portENTER_CRITICAL(&mux_);
    chunkCount_++;
    if (!active_) {
        cur_ = MessCmd{};
        cur_.t0Us = now;
        cur_.gapUs = lastCompleteUs_ ? (now - lastCompleteUs_) : 0;
        cur_.lba = lba;
        cur_.op = 0x28;
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
    const uint32_t now = micros();
    portENTER_CRITICAL(&mux_);
    chunkCount_++;
    if (!active_) {
        cur_ = MessCmd{};
        cur_.t0Us = now;
        cur_.gapUs = lastCompleteUs_ ? (now - lastCompleteUs_) : 0;
        cur_.lba = lba;
        cur_.op = 0x2A;
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
    MessCmd local[16];
    size_t n = 0;
    portENTER_CRITICAL(&mux_);
    n = pendingEmitN_;
    if (n) {
        memcpy(local, pendingEmit_, n * sizeof(MessCmd));
        pendingEmitN_ = 0;
    }
    portEXIT_CRITICAL(&mux_);
    if (!emit_) return;
    for (size_t i = 0; i < n; i++) emit_(local[i]);
}

void MessCapture::toJson(JsonObject obj) const {
    obj["enabled"] = enabled_;
    obj["usbAttached"] = usbAttached_;
    obj["usbDetachN"] = usbDetachCount_;
    obj["usbAttachN"] = usbAttachCount_;
    obj["cmds"] = cmdCount_;
    obj["drops"] = dropCount_;
    obj["chunks"] = chunkCount_;
    obj["ring"] = (uint32_t)count_;
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
