#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "core/EventLog.h"
#include "core/MenuStore.h"
#include "core/StreamBuffer.h"
#include <functional>

struct MscFileMap {
    uint32_t lbaStart = 0;
    uint32_t lbaEnd = 0;
    char uid[24] = {0};
    char name[40] = {0};
    char path[48] = {0};
    bool active = false;
};

struct MscReadSample {
    uint32_t ms = 0;
    uint32_t lba = 0;
    uint16_t bytes = 0;
    uint8_t kind = 0;
    char tag[12] = {0};
};

/** Tunable looksLikePlay thresholds (from ConfigStore / SoftAP). */
struct PlayDetectParams {
    uint16_t plugWindowMs = 2500;
    uint16_t minSeqBytes = 6000;
    uint8_t headLbaSlop = 12;
    uint16_t cooldownMs = 5000;
    uint8_t prefetchLbaSlop = 2;
};

class UsbMscGadget {
public:
    static constexpr size_t kTraceSize = 24;
    static constexpr size_t kSlots = 4;
    static constexpr uint32_t kDataStartLba = 35;
    static constexpr uint8_t kSpc = 4;  // sectors per cluster (demo FAT)
    /** Wait for menu_set before mediaPresent; then show last-known/demo. */
    static constexpr uint32_t kMediaPresentTimeoutMs = 7000;

    using PlayHandler = std::function<void(const char* uid)>;

    bool begin(EventLog* events, MenuStore* menu);
    void loop();
    /** Apply menu names to slots; presents media on first apply / after wait. */
    void applyMenuSlots(const MenuStore& menu);
    /** Show MSC medium to host (idempotent). */
    void presentMedia(const char* reason);
    bool mediaPresented() const { return mediaPresented_; }
    void setPlayHandler(PlayHandler h) { playHandler_ = h; }
    void setStreamBuffer(StreamBuffer* s) { stream_ = s; }
    void setPlayDetectParams(const PlayDetectParams& p) { playDetect_ = p; }
    PlayDetectParams playDetectParams() const { return playDetect_; }
    void startStream(const char* uid);
    void stopStream();

    bool ready() const { return ready_; }
    bool plugged() const { return plugged_; }
    bool suspended() const { return suspended_; }
    uint32_t readCount() const { return readCount_; }
    uint32_t lastReadLba() const { return lastReadLba_; }
    uint32_t msSincePlug() const;
    uint32_t msSinceChange() const;
    uint32_t msPlugToFirstRead() const { return msPlugToFirstRead_; }
    uint32_t msPlugToPlayGuess() const { return msPlugToPlayGuess_; }
    uint32_t plugCount() const { return plugCount_; }
    uint32_t unplugCount() const { return unplugCount_; }
    void toJson(JsonObject obj) const;
    void traceToJson(JsonArray arr) const;

    void onUsbPlugged(bool on);
    void onUsbSuspend(bool on);
    void onHostStartStop(bool start, bool loadEject);
    int32_t onRead(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize);
    int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize);

private:
    enum class Region : uint8_t { Meta, Dir, File };
    enum class PlayEval : uint8_t {
        Ok = 0,
        BadFile,
        NotFromHead,
        PlugWindow,
        SeqShort,
    };

    void noteDataRead(uint32_t lba, uint32_t bufsize);
    void pushTrace(uint32_t lba, uint32_t bufsize, uint8_t kind, const char* tag);
    Region classify(uint32_t lba, const MscFileMap** fileOut) const;
    const MscFileMap* fileForLba(uint32_t lba) const;
    PlayEval evaluatePlay(const MscFileMap* f, uint32_t startLba, uint32_t seqBytes) const;
    static const char* playEvalName(PlayEval e);
    void emitPlayReject(PlayEval eval, const MscFileMap* f, uint32_t startLba, uint32_t seqBytes,
                        const char* extra = nullptr);
    void loadDefaultSlots();
    /** Patch STATIONS/SETTINGS names; sizes/chains always from fixed slot geometry. */
    void patchDirNames(uint8_t* sector, uint32_t lba) const;
    void patchFatFixed(uint8_t* sector, uint32_t lba) const;
    void patchFatChain(uint8_t* sector, uint32_t lba, uint16_t cl0, uint16_t cl1) const;
    static uint16_t lbaToCluster(uint32_t lba) {
        if (lba < kDataStartLba) return 0;
        return (uint16_t)(2 + (lba - kDataStartLba) / kSpc);
    }
    static uint32_t slotBytes(const MscFileMap& s) {
        if (s.lbaEnd < s.lbaStart) return 0;
        return (s.lbaEnd - s.lbaStart + 1) * 512u;
    }

    EventLog* events_ = nullptr;
    MenuStore* menu_ = nullptr;
    StreamBuffer* stream_ = nullptr;
    PlayHandler playHandler_;
    MscFileMap slots_[kSlots];
    PlayDetectParams playDetect_;

    /** Which slot's payload is live audio — does NOT change FAT/dir geometry. */
    int streamSlot_ = -1;

    bool ready_ = false;
    bool plugged_ = false;
    bool suspended_ = false;
    uint32_t plugMs_ = 0;
    uint32_t changeMs_ = 0;
    uint32_t firstReadMs_ = 0;
    uint32_t playGuessMs_ = 0;
    uint32_t msPlugToFirstRead_ = 0;
    uint32_t msPlugToPlayGuess_ = 0;
    uint32_t readCount_ = 0;
    uint32_t writeCount_ = 0;
    uint32_t writeRejectCount_ = 0;
    uint32_t lastReadLba_ = 0;
    uint32_t bytesRead_ = 0;
    uint32_t bytesMeta_ = 0;
    uint32_t bytesFile_ = 0;
    uint32_t seqBytes_ = 0;
    uint32_t seqLba_ = 0;
    uint32_t seqStartLba_ = 0;
    const MscFileMap* seqFile_ = nullptr;
    uint32_t lastEventMs_ = 0;
    uint32_t lastTraceLogMs_ = 0;
    uint32_t lastRejectMs_ = 0;
    uint8_t lastRejectEval_ = 0xFF;
    uint32_t plugCount_ = 0;
    uint32_t unplugCount_ = 0;
    uint32_t prefetchHits_ = 0;
    uint32_t playRejectCount_ = 0;
    uint32_t playGuessCount_ = 0;
    uint32_t streamBytesServed_ = 0;
    bool mediaPresented_ = false;
    uint32_t presentDeadlineMs_ = 0;

    MscReadSample trace_[kTraceSize];
    size_t traceHead_ = 0;
    size_t traceCount_ = 0;
};
