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
using ProbeClock = int64_t;

enum class MessSource : uint8_t {
    Synthesized = 0,
    Marker = 1,
};

enum class MessCaptureMode : uint8_t {
    None = 0,
    RamLocal = 1,
    RamWifiOff = 2,
};

struct MessCmd {
    uint64_t seq = 0;
    ProbeClock t0Us = 0;      // monotonic, wrap-extended micros
    ProbeClock t1Us = 0;
    ProbeClock gapUs = 0;
    uint32_t lba = 0;
    uint32_t bytes = 0;      // delivered (sum of positive chunk returns)
    uint16_t chunks = 0;
    uint16_t busyN = 0;      // chunk returns == 0 (BUSY retry)
    uint8_t op = 0;          // 0x28 READ10, 0x2A WRITE10, 0xFF mark, …
    uint8_t status = 0;      // 0 ok, 1 incomplete/overflow flush, 2 mark
    MessSource source = MessSource::Synthesized;
    bool synthesized = false;
    char tag[12] = {0};      // optional situation / mark label
};

class MessCapture {
public:
#if defined(PIDRIVE_MESS) && PIDRIVE_MESS
    static constexpr size_t kRing = 512;  // ~32 KiB; bounded DRAM journal for field windows
#else
    static constexpr size_t kRing = 128;
#endif
    using EmitHandler = std::function<void(const MessCmd&)>;

    static MessCapture& instance();

    void setEnabled(bool on);
    bool enabled() const { return enabled_; }
    void setEmitHandler(EmitHandler h) { emit_ = h; }

    void reset();
    void mark(const char* tag);
    /** Start a RAM-only capture. Both modes suppress live emission and command HTTP dumps. */
    bool beginCapture(MessCaptureMode mode);
    void endCapture(const char* reason);
    bool captureActive() const { return captureActive_; }
    MessCaptureMode captureMode() const { return captureMode_; }
    const char* captureModeName() const;

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
    /** Replay records with seq > afterSeq. Oldest available records are returned first. */
    size_t copyAfter(uint64_t afterSeq, MessCmd* out, size_t maxCmds) const;

    uint32_t cmdCount() const { return cmdCount_; }
    uint32_t dropCount() const { return dropCount_; }
    uint32_t chunkCount() const { return chunkCount_; }
    uint64_t newestSeq() const { return nextSeq_ ? nextSeq_ - 1 : 0; }

private:
    MessCapture() = default;
    void pushCmd(const MessCmd& c);
    void flushActive(uint8_t status);
    ProbeClock nowUsLocked();

    bool enabled_ = true;
    bool captureActive_ = false;
    bool captureComplete_ = false;
    MessCaptureMode captureMode_ = MessCaptureMode::None;
    uint64_t captureStartSeq_ = 0;
    uint64_t captureEndSeq_ = 0;
    uint32_t captureStartDrops_ = 0;
    uint32_t captureEndDrops_ = 0;
    char captureEndReason_[24] = {0};
    bool usbAttached_ = true;
    uint32_t usbDetachCount_ = 0;
    uint32_t usbAttachCount_ = 0;
    uint32_t usbReattachAtMs_ = 0;

    bool active_ = false;
    MessCmd cur_{};
    ProbeClock lastCompleteUs_ = 0;
    uint32_t clockLastRawUs_ = 0;
    uint64_t clockEpochUs_ = 0;

    MessCmd ring_[kRing];
    size_t head_ = 0;
    size_t count_ = 0;
    uint32_t cmdCount_ = 0;
    uint32_t dropCount_ = 0;
    uint32_t chunkCount_ = 0;
    uint64_t nextSeq_ = 1;
    uint64_t emitSeq_ = 0;
    uint32_t emitDropCount_ = 0;
    mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;

    EmitHandler emit_;
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
