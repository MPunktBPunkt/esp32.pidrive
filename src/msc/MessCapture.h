#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <functional>

/**
 * HU / host measurement capture (mess-fw).
 *
 * Reconstructs READ10 command boundaries via TinyUSB:
 *   wrap tud_msc_read10_cb (chunks) + strong tud_msc_read10_complete_cb.
 * That yields Q10-relevant cmd_bytes / blocks without forking TinyUSB CBW structs.
 *
 * Soft USB attach/detach uses tud_disconnect/tud_connect (D+ pull-up) — NOT VBUS.
 */
struct MessCmd {
    uint32_t t0Us = 0;       // micros() at first chunk (or SCSI note)
    uint32_t t1Us = 0;       // micros() at complete / last chunk
    uint32_t gapUs = 0;      // previous complete → this start
    uint32_t lba = 0;
    uint32_t bytes = 0;      // delivered (sum of positive chunk returns)
    uint16_t chunks = 0;
    uint16_t busyN = 0;      // chunk returns == 0 (BUSY retry)
    uint8_t op = 0;          // 0x28 READ10, 0x2A WRITE10, 0xFF mark, …
    uint8_t status = 0;      // 0 ok, 1 incomplete/overflow flush, 2 mark
    char tag[12] = {0};      // optional situation / mark label
};

class MessCapture {
public:
    static constexpr size_t kRing = 128;
    using EmitHandler = std::function<void(const MessCmd&)>;

    static MessCapture& instance();

    void setEnabled(bool on);
    bool enabled() const { return enabled_; }
    void setEmitHandler(EmitHandler h) { emit_ = h; }

    void reset();
    void mark(const char* tag);

    /** USB soft bus (D+). holdMs before auto-reattach; 0 = stay detached. */
    bool usbDetach(uint16_t holdMs = 0);
    bool usbAttach();
    bool usbAttached() const { return usbAttached_; }
    uint32_t usbDetachCount() const { return usbDetachCount_; }
    uint32_t usbAttachCount() const { return usbAttachCount_; }

    void noteRead10Chunk(uint32_t lba, uint32_t offset, uint32_t bufsize, int32_t ret);
    void noteRead10Complete(uint8_t lun);
    void noteWrite10Chunk(uint32_t lba, uint32_t offset, uint32_t bufsize, int32_t ret);
    void noteWrite10Complete(uint8_t lun);

    void loop();  // drain ring → emit handler
    void toJson(JsonObject obj) const;
    /** Copy up to maxCmds from ring (newest last). Returns count. */
    size_t copyRecent(MessCmd* out, size_t maxCmds) const;

    uint32_t cmdCount() const { return cmdCount_; }
    uint32_t dropCount() const { return dropCount_; }
    uint32_t chunkCount() const { return chunkCount_; }

private:
    MessCapture() = default;
    void pushCmd(const MessCmd& c);
    void flushActive(uint8_t status);

    bool enabled_ = true;
    bool usbAttached_ = true;
    uint32_t usbDetachCount_ = 0;
    uint32_t usbAttachCount_ = 0;
    uint32_t usbReattachAtMs_ = 0;

    bool active_ = false;
    MessCmd cur_{};
    uint32_t lastCompleteUs_ = 0;

    MessCmd ring_[kRing];
    size_t head_ = 0;
    size_t count_ = 0;
    uint32_t cmdCount_ = 0;
    uint32_t dropCount_ = 0;
    uint32_t chunkCount_ = 0;
    mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;

    EmitHandler emit_;
    MessCmd pendingEmit_[16];
    size_t pendingEmitN_ = 0;
};

#if defined(PIDRIVE_MESS) && PIDRIVE_MESS
extern "C" {
int32_t __wrap_tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void* buffer,
                                 uint32_t bufsize);
int32_t __wrap_tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t* buffer,
                                  uint32_t bufsize);
void tud_msc_read10_complete_cb(uint8_t lun);
void tud_msc_write10_complete_cb(uint8_t lun);
}
#endif
