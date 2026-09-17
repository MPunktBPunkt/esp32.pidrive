#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "core/EventLog.h"
#include "core/MenuStore.h"

struct MscFileMap {
    uint32_t lbaStart;
    uint32_t lbaEnd;  // inclusive
    const char* uid;
    const char* name;
    const char* path;
};

struct MscReadSample {
    uint32_t ms = 0;
    uint32_t lba = 0;
    uint16_t bytes = 0;
    uint8_t kind = 0;  // 0=meta 1=dir 2=file 3=write
    char tag[12] = {0};
};

class UsbMscGadget {
public:
    static constexpr size_t kTraceSize = 24;
    static constexpr uint32_t kDataStartLba = 35;  // FAT12 demo geometry

    bool begin(EventLog* events, MenuStore* menu);
    void loop();
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

    void noteDataRead(uint32_t lba, uint32_t bufsize);
    void pushTrace(uint32_t lba, uint32_t bufsize, uint8_t kind, const char* tag);
    Region classify(uint32_t lba, const MscFileMap** fileOut) const;
    const MscFileMap* fileForLba(uint32_t lba) const;
    bool looksLikePlay(const MscFileMap* f, uint32_t startLba, uint32_t seqBytes) const;

    EventLog* events_ = nullptr;
    MenuStore* menu_ = nullptr;
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
    uint32_t plugCount_ = 0;
    uint32_t unplugCount_ = 0;
    uint32_t prefetchHits_ = 0;

    MscReadSample trace_[kTraceSize];
    size_t traceHead_ = 0;
    size_t traceCount_ = 0;
};
