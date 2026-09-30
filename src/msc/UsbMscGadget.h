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
    char kind[12] = {0};  // station | action | folder | info (from menu_set)
    bool active = false;
};

struct MscReadSample {
    uint32_t ms = 0;
    uint32_t lba = 0;
    uint16_t bytes = 0;
    uint16_t gapMs = 0;
    uint8_t kind = 0;  // 0 meta / 1 dir / 2 file / 3 wr / 4 boot / 5 fat
    char tag[12] = {0};
};

struct MscSlotStats {
    uint32_t bytes = 0;
    uint32_t fromHeadHits = 0;
    uint32_t midFileHits = 0;
    uint32_t maxSeqBytes = 0;
    uint32_t lastReadMs = 0;
};

/** Tunable looksLikePlay thresholds (from ConfigStore / SoftAP). */
struct PlayDetectParams {
    uint16_t plugWindowMs = 500;   // was 2500 — index window; BMW still needs large slots
    uint16_t minSeqBytes = 6000;   // stations (live overlay)
    /** After indexSettled: action/folder/pump:* — BMW often only issues one 4 KiB head read. */
    uint16_t navMinSeqBytes = 4096;
    uint8_t headLbaSlop = 12;
    uint16_t cooldownMs = 5000;
    uint8_t prefetchLbaSlop = 2;
};

class UsbMscGadget {
public:
    static constexpr size_t kTraceSize = 96;
    static constexpr size_t kSlots = 4;
    /** Virtual MSC capacity (2 MiB) — larger than demo_fat.bin; LBAs beyond image are synthesized. */
    static constexpr uint32_t kVirtSectorCount = 4096;
    static constexpr uint8_t kFatSpf = 8;           // sectors/FAT (FAT12 can address 512KiB slots)
    static constexpr uint32_t kFat0Lba = 1;         // LBA 1..8
    static constexpr uint32_t kFat1Lba = 9;         // LBA 9..16
    static constexpr uint32_t kRootLba0 = 17;       // 32 sectors (512 ents)
    static constexpr uint32_t kRootSectors = 32;
    static constexpr uint32_t kDataStartLba = 49;   // cluster 2
    static constexpr uint8_t kSpc = 4;              // sectors per cluster
    static constexpr uint32_t kStationsLba = 49;    // cluster 2
    static constexpr uint32_t kSettingsLba = 53;    // cluster 3
    /** ~512 KiB station stubs so HU cannot cache the whole file at index. */
    static constexpr uint32_t kSlotSectors = 1024;
    /** Wait for menu_set before mediaPresent; then show last-known/demo. */
    static constexpr uint32_t kMediaPresentTimeoutMs = 7000;
    static constexpr uint32_t kQuietAfterFileMs = 2000;

    using PlayHandler = std::function<void(const char* uid)>;
    using DiagHandler = std::function<void(const char* code, const char* detail)>;

    bool begin(EventLog* events, MenuStore* menu);
    void loop();
    /** Apply menu names to slots; presents media on first apply / after wait. */
    void applyMenuSlots(const MenuStore& menu);
    /** Show MSC medium to host (idempotent). */
    void presentMedia(const char* reason);
    /** Hide briefly so host re-reads DIR after menu name changes. */
    void remountMedia(const char* reason);
    bool mediaPresented() const { return mediaPresented_; }
    void setPlayHandler(PlayHandler h) { playHandler_ = h; }
    void setDiagHandler(DiagHandler h) { diagHandler_ = h; }
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
    void onHostStartStop(uint8_t powerCondition, bool start, bool loadEject);
    int32_t onRead(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize);
    int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize);

private:
    enum class Region : uint8_t { Boot, Fat, Dir, File, Meta };
    enum class PlayEval : uint8_t {
        Ok = 0,
        BadFile,
        NotFromHead,
        PlugWindow,
        SeqShort,
    };
    enum class Phase : uint8_t {
        Idle = 0,
        Scan,
        Index,
        Play,
        Quiet,
    };

    void noteDataRead(uint32_t lba, uint32_t bufsize);
    void pushTrace(uint32_t lba, uint32_t bufsize, uint8_t kind, const char* tag);
    void noteXferSize(uint32_t bufsize);
    void setPhase(Phase p, const char* detail);
    static const char* phaseName(Phase p);
    Region classify(uint32_t lba, const MscFileMap** fileOut) const;
    const MscFileMap* fileForLba(uint32_t lba) const;
    int slotIndex(const MscFileMap* f) const;
    PlayEval evaluatePlay(const MscFileMap* f, uint32_t startLba, uint32_t seqBytes) const;
    /** action/folder/pump:* — menu navigation, not live audio. */
    static bool isNavSlot(const MscFileMap* f);
    uint32_t minSeqFor(const MscFileMap* f) const;
    static const char* playEvalName(PlayEval e);
    void emitPlayReject(PlayEval eval, const MscFileMap* f, uint32_t startLba, uint32_t seqBytes,
                        const char* extra = nullptr);
    void loadDefaultSlots();
    /** Bump USB serialNumber + productRevision (HU MediaStore cache key). */
    void applyUsbIdentity();
    void loadRemountGenNvs();
    void saveRemountGenNvs() const;
    static void formatPdSerial(uint16_t gen, char* ser, size_t n);
    /** Patch boot BPB to virtual geometry; synthesize root; STATIONS/SETTINGS LFNs. */
    void patchBoot(uint8_t* sector) const;
    void patchRootDir(uint8_t* sector, uint32_t lba) const;
    void patchDirNames(uint8_t* sector, uint32_t lba) const;
    void patchFatFixed(uint8_t* sector, uint32_t lba) const;
    void patchFatChain(uint8_t* sector, uint32_t lba, uint16_t cl0, uint16_t cl1) const;
    static void slotRanges(uint32_t out[kSlots][2]) {
        // After STATIONS/SETTINGS clusters (49-56): three 512KiB slots + short page slot.
        const uint32_t a0 = kSettingsLba + kSpc;  // 57
        out[0][0] = a0;
        out[0][1] = a0 + kSlotSectors - 1;
        out[1][0] = out[0][1] + 1;
        out[1][1] = out[1][0] + kSlotSectors - 1;
        out[2][0] = out[1][1] + 1;
        out[2][1] = out[2][0] + kSlotSectors - 1;
        out[3][0] = out[2][1] + 1;
        out[3][1] = out[3][0] + 12;
    }
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
    DiagHandler diagHandler_;
    void emitDiag(const char* code, const char* detail = "");
    MscFileMap slots_[kSlots];
    MscSlotStats slotStats_[kSlots];
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
    uint32_t lastReadMs_ = 0;
    uint32_t lastFileReadMs_ = 0;
    uint32_t bytesRead_ = 0;
    uint32_t bytesMeta_ = 0;
    uint32_t bytesFile_ = 0;
    uint32_t bytesBoot_ = 0;
    uint32_t bytesFat_ = 0;
    uint32_t bytesDir_ = 0;
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
    uint32_t remountHoldUntilMs_ = 0;
    uint16_t remountGen_ = 0;  // bumps USB serial + volume label (BMW MediaStore cache)
    Phase phase_ = Phase::Idle;
    bool quietEmitted_ = false;
    /** Set on first msc.quiet after plug — blocks play.guess during deep 512KiB index. */
    bool indexSettled_ = false;

    // Transfer-size buckets (host buffering fingerprint).
    uint32_t xfer512_ = 0;
    uint32_t xfer2k_ = 0;
    uint32_t xfer4k_ = 0;
    uint32_t xfer8kPlus_ = 0;

    MscReadSample trace_[kTraceSize];
    size_t traceHead_ = 0;
    size_t traceCount_ = 0;
};
