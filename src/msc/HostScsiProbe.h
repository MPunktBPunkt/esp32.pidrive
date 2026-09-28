#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "core/EventLog.h"

/**
 * SCSI-side host fingerprint (BMW vs Linux vs phone).
 * Arduino USBMSC owns the real tud_msc_* callbacks; we intercept via
 * linker --wrap and count/timing without forking the framework.
 */
class HostScsiProbe {
public:
    void begin(EventLog* events);
    void onPlug(uint32_t plugMs);
    void noteInquiry();
    void noteCapacity();
    void noteTestUnitReady(bool ready);
    void noteScsi(uint8_t opcode);
    void noteStartStop(uint8_t powerCondition, bool start, bool loadEject);
    void toJson(JsonObject obj) const;
    void resetSessionCounters();

    uint32_t inquiryCount() const { return inquiry_; }
    uint32_t capacityCount() const { return capacity_; }
    uint32_t turCount() const { return tur_; }
    uint32_t preventCount() const { return prevent_; }
    uint32_t otherScsiCount() const { return otherScsi_; }

    static HostScsiProbe& instance();

private:
    void maybeEvent(const char* code, const char* detail);

    EventLog* events_ = nullptr;
    uint32_t plugMs_ = 0;
    uint32_t lastEventMs_ = 0;

    uint32_t inquiry_ = 0;
    uint32_t capacity_ = 0;
    uint32_t tur_ = 0;
    uint32_t turNotReady_ = 0;
    uint32_t prevent_ = 0;
    uint32_t otherScsi_ = 0;
    uint32_t startStop_ = 0;
    uint8_t lastOpcode_ = 0;
    uint32_t msToFirstInquiry_ = 0;
    uint32_t msToFirstCapacity_ = 0;
    uint32_t msToFirstTur_ = 0;
};

extern "C" {
void __wrap_tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16],
                               uint8_t product_rev[4]);
void __wrap_tud_msc_capacity_cb(uint8_t lun, uint32_t* block_count, uint16_t* block_size);
bool __wrap_tud_msc_test_unit_ready_cb(uint8_t lun);
int32_t __wrap_tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void* buffer,
                               uint16_t bufsize);
}
