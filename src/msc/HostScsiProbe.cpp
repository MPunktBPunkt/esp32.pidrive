#include "HostScsiProbe.h"
#include <cstring>

#if !ARDUINO_USB_MODE

// Real symbols provided by Arduino USBMSC.cpp via --wrap.
extern "C" {
void __real_tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16],
                               uint8_t product_rev[4]);
void __real_tud_msc_capacity_cb(uint8_t lun, uint32_t* block_count, uint16_t* block_size);
bool __real_tud_msc_test_unit_ready_cb(uint8_t lun);
int32_t __real_tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void* buffer,
                               uint16_t bufsize);
}

#ifndef SCSI_CMD_PREVENT_ALLOW_MEDIUM_REMOVAL
#define SCSI_CMD_PREVENT_ALLOW_MEDIUM_REMOVAL 0x1E
#endif

HostScsiProbe& HostScsiProbe::instance() {
    static HostScsiProbe probe;
    return probe;
}

void HostScsiProbe::begin(EventLog* events) {
    events_ = events;
}

void HostScsiProbe::resetSessionCounters() {
    inquiry_ = 0;
    capacity_ = 0;
    tur_ = 0;
    turNotReady_ = 0;
    prevent_ = 0;
    otherScsi_ = 0;
    startStop_ = 0;
    lastOpcode_ = 0;
    msToFirstInquiry_ = 0;
    msToFirstCapacity_ = 0;
    msToFirstTur_ = 0;
}

void HostScsiProbe::onPlug(uint32_t plugMs) {
    plugMs_ = plugMs;
    resetSessionCounters();
}

void HostScsiProbe::maybeEvent(const char* code, const char* detail) {
    if (!events_) return;
    if (millis() - lastEventMs_ < 150) return;
    lastEventMs_ = millis();
    events_->push(code, detail);
}

void HostScsiProbe::noteInquiry() {
    inquiry_++;
    if (plugMs_ && !msToFirstInquiry_) {
        msToFirstInquiry_ = millis() - plugMs_;
        char d[40];
        snprintf(d, sizeof(d), "t=%lums n=%lu", (unsigned long)msToFirstInquiry_,
                 (unsigned long)inquiry_);
        maybeEvent("msc.scsi.inquiry", d);
    }
}

void HostScsiProbe::noteCapacity() {
    capacity_++;
    if (plugMs_ && !msToFirstCapacity_) {
        msToFirstCapacity_ = millis() - plugMs_;
        char d[40];
        snprintf(d, sizeof(d), "t=%lums n=%lu", (unsigned long)msToFirstCapacity_,
                 (unsigned long)capacity_);
        maybeEvent("msc.scsi.capacity", d);
    }
}

void HostScsiProbe::noteTestUnitReady(bool ready) {
    tur_++;
    if (!ready) turNotReady_++;
    if (plugMs_ && !msToFirstTur_) {
        msToFirstTur_ = millis() - plugMs_;
        char d[48];
        snprintf(d, sizeof(d), "t=%lums ready=%u", (unsigned long)msToFirstTur_, ready ? 1 : 0);
        maybeEvent("msc.scsi.tur", d);
    }
}

void HostScsiProbe::noteScsi(uint8_t opcode) {
    lastOpcode_ = opcode;
    if (opcode == SCSI_CMD_PREVENT_ALLOW_MEDIUM_REMOVAL) {
        prevent_++;
        if (prevent_ == 1 || (prevent_ % 8) == 0) {
            char d[24];
            snprintf(d, sizeof(d), "n=%lu", (unsigned long)prevent_);
            maybeEvent("msc.scsi.prevent", d);
        }
        return;
    }
    otherScsi_++;
    char d[32];
    snprintf(d, sizeof(d), "op=0x%02X n=%lu", opcode, (unsigned long)otherScsi_);
    maybeEvent("msc.scsi.other", d);
}

void HostScsiProbe::noteStartStop(uint8_t powerCondition, bool start, bool loadEject) {
    startStop_++;
    char d[48];
    snprintf(d, sizeof(d), "pwr=%u start=%u eject=%u", (unsigned)powerCondition, start ? 1 : 0,
             loadEject ? 1 : 0);
    maybeEvent(start ? "msc.host.start" : "msc.host.stop", d);
}

void HostScsiProbe::toJson(JsonObject obj) const {
    obj["inquiry"] = inquiry_;
    obj["capacity"] = capacity_;
    obj["tur"] = tur_;
    obj["turNotReady"] = turNotReady_;
    obj["prevent"] = prevent_;
    obj["otherScsi"] = otherScsi_;
    obj["startStop"] = startStop_;
    obj["lastOpcode"] = lastOpcode_;
    obj["msToInquiry"] = msToFirstInquiry_;
    obj["msToCapacity"] = msToFirstCapacity_;
    obj["msToTur"] = msToFirstTur_;
    // Rough stack hint from command mix (field heuristic, not gospel).
    const char* hint = "unknown";
    if (inquiry_ || capacity_ || tur_) {
        if (prevent_ > 0 && inquiry_ <= 2 && capacity_ <= 4)
            hint = "hu-like";  // automotive stacks often PREVENT + few inquiries
        else if (tur_ > 20 && inquiry_ <= 2)
            hint = "poll-heavy";  // frequent TUR (some Android/desktop)
        else if (inquiry_ >= 3)
            hint = "reprobe";  // remount / multi-pass probe
        else
            hint = "generic";
    }
    obj["hint"] = hint;
}

extern "C" void __wrap_tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16],
                                          uint8_t product_rev[4]) {
    HostScsiProbe::instance().noteInquiry();
    __real_tud_msc_inquiry_cb(lun, vendor_id, product_id, product_rev);
}

extern "C" void __wrap_tud_msc_capacity_cb(uint8_t lun, uint32_t* block_count, uint16_t* block_size) {
    HostScsiProbe::instance().noteCapacity();
    __real_tud_msc_capacity_cb(lun, block_count, block_size);
}

extern "C" bool __wrap_tud_msc_test_unit_ready_cb(uint8_t lun) {
    bool ready = __real_tud_msc_test_unit_ready_cb(lun);
    HostScsiProbe::instance().noteTestUnitReady(ready);
    return ready;
}

extern "C" int32_t __wrap_tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void* buffer,
                                          uint16_t bufsize) {
    if (scsi_cmd) HostScsiProbe::instance().noteScsi(scsi_cmd[0]);
    return __real_tud_msc_scsi_cb(lun, scsi_cmd, buffer, bufsize);
}

#else

HostScsiProbe& HostScsiProbe::instance() {
    static HostScsiProbe probe;
    return probe;
}
void HostScsiProbe::begin(EventLog*) {}
void HostScsiProbe::onPlug(uint32_t) {}
void HostScsiProbe::noteInquiry() {}
void HostScsiProbe::noteCapacity() {}
void HostScsiProbe::noteTestUnitReady(bool) {}
void HostScsiProbe::noteScsi(uint8_t) {}
void HostScsiProbe::noteStartStop(uint8_t, bool, bool) {}
void HostScsiProbe::toJson(JsonObject obj) const { obj["hint"] = "usb-mode-off"; }
void HostScsiProbe::resetSessionCounters() {}

#endif
