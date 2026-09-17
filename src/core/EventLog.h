#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "BuildFlags.h"

struct EventEntry {
    uint32_t seq = 0;
    uint32_t ms = 0;
    char code[24] = {0};
    char detail[96] = {0};
};

class EventLog {
public:
    void begin();
    void push(const char* code, const char* detail = "");
    void clear();
    uint32_t nextSeq() const { return seq_; }
    void toJsonArray(JsonArray arr, uint32_t sinceSeq) const;
    size_t count() const { return count_; }

private:
    EventEntry ring_[EVENT_RING_SIZE];
    size_t head_ = 0;
    size_t count_ = 0;
    uint32_t seq_ = 1;
};
