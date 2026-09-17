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

class UsbMscGadget {
public:
    bool begin(EventLog* events, MenuStore* menu);
    void loop();
    bool ready() const { return ready_; }
    bool plugged() const { return plugged_; }
    uint32_t readCount() const { return readCount_; }
    uint32_t lastReadLba() const { return lastReadLba_; }
    uint32_t msSincePlug() const;
    uint32_t msPlugToFirstRead() const { return msPlugToFirstRead_; }
    uint32_t msPlugToPlayGuess() const { return msPlugToPlayGuess_; }
    void toJson(JsonObject obj) const;

    // called from USB callbacks (static trampolines)
    void onUsbPlugged(bool on);
    int32_t onRead(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize);
    int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize);

private:
    void noteDataRead(uint32_t lba, uint32_t bufsize);
    const MscFileMap* fileForLba(uint32_t lba) const;

    EventLog* events_ = nullptr;
    MenuStore* menu_ = nullptr;
    bool ready_ = false;
    bool plugged_ = false;
    uint32_t plugMs_ = 0;
    uint32_t firstReadMs_ = 0;
    uint32_t playGuessMs_ = 0;
    uint32_t msPlugToFirstRead_ = 0;
    uint32_t msPlugToPlayGuess_ = 0;
    uint32_t readCount_ = 0;
    uint32_t lastReadLba_ = 0;
    uint32_t seqBytes_ = 0;
    uint32_t seqLba_ = 0;
    const MscFileMap* seqFile_ = nullptr;
    uint32_t lastEventMs_ = 0;
};
