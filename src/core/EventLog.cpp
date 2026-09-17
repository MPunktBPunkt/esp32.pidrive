#include "EventLog.h"
#include <cstring>

void EventLog::begin() {
    head_ = 0;
    count_ = 0;
    seq_ = 1;
}

void EventLog::push(const char* code, const char* detail) {
    EventEntry& e = ring_[head_];
    e.seq = seq_++;
    e.ms = millis();
    strncpy(e.code, code ? code : "?", sizeof(e.code) - 1);
    e.code[sizeof(e.code) - 1] = 0;
    strncpy(e.detail, detail ? detail : "", sizeof(e.detail) - 1);
    e.detail[sizeof(e.detail) - 1] = 0;
    head_ = (head_ + 1) % EVENT_RING_SIZE;
    if (count_ < EVENT_RING_SIZE) count_++;
    Serial.printf("[EVT] %s %s\n", e.code, e.detail);
}

void EventLog::clear() {
    head_ = 0;
    count_ = 0;
}

void EventLog::toJsonArray(JsonArray arr, uint32_t sinceSeq) const {
    if (count_ == 0) return;
    size_t start = (head_ + EVENT_RING_SIZE - count_) % EVENT_RING_SIZE;
    for (size_t i = 0; i < count_; i++) {
        const EventEntry& e = ring_[(start + i) % EVENT_RING_SIZE];
        if (e.seq <= sinceSeq) continue;
        JsonObject o = arr.add<JsonObject>();
        o["seq"] = e.seq;
        o["ms"] = e.ms;
        o["code"] = e.code;
        o["detail"] = e.detail;
    }
}
