#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "core/EventLog.h"

/**
 * UART-USB link state (activity). RX bytes are fed via noteRx() from PumpServer
 * so only one consumer reads Serial.
 */
class UartLinkMonitor {
public:
    enum class State : uint8_t { Idle = 0, Up, Quiet };

    void begin(EventLog* events);
    void loop();
    void noteRx(size_t n);
    bool linkUp() const { return state_ == State::Up; }
    State state() const { return state_; }
    const char* stateName() const;
    uint32_t rxBytes() const { return rxBytes_; }
    uint32_t lastRxMs() const { return lastRxMs_; }
    uint32_t msSinceChange() const;
    uint32_t msSinceRx() const;
    uint32_t changeCount() const { return changeCount_; }
    void toJson(JsonObject obj) const;

private:
    void setState(State s, const char* detail);

    EventLog* events_ = nullptr;
    State state_ = State::Idle;
    uint32_t lastRxMs_ = 0;
    uint32_t changeMs_ = 0;
    uint32_t changeCount_ = 0;
    uint32_t rxBytes_ = 0;
    static constexpr uint32_t kIdleMs = 4000;
};
