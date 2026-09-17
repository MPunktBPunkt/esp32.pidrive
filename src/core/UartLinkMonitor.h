#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "core/EventLog.h"

/**
 * UART-USB (Bridge-Chip → Pi/PC): kein nativer Plug-Sensor am ESP.
 * Wir spiegeln Seriellaktivität als „Link“ (später PUMP-Hello).
 */
class UartLinkMonitor {
public:
    void begin(EventLog* events);
    void loop();
    bool linkUp() const { return linkUp_; }
    uint32_t rxBytes() const { return rxBytes_; }
    uint32_t lastRxMs() const { return lastRxMs_; }
    uint32_t msSinceChange() const;
    uint32_t changeCount() const { return changeCount_; }
    void toJson(JsonObject obj) const;

private:
    void setLink(bool up, const char* detail);

    EventLog* events_ = nullptr;
    bool linkUp_ = false;
    uint32_t lastRxMs_ = 0;
    uint32_t changeMs_ = 0;
    uint32_t changeCount_ = 0;
    uint32_t rxBytes_ = 0;
    static constexpr uint32_t kIdleMs = 4000;
};
