#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "core/EventLog.h"
#include "core/MenuStore.h"
#include "core/UartLinkMonitor.h"
#include "msc/UsbMscGadget.h"

/** Line-JSON PUMP stub over UART (Pi ↔ ESP). */
class PumpServer {
public:
    void begin(EventLog* events, MenuStore* menu, UsbMscGadget* msc, UartLinkMonitor* uart);
    void loop();
    bool up() const { return up_; }
    void sendPlayUid(const char* uid);
    void sendJson(const JsonDocument& doc);

private:
    void handleLine(char* line);
    void sendRaw(const char* s);

    EventLog* events_ = nullptr;
    MenuStore* menu_ = nullptr;
    UsbMscGadget* msc_ = nullptr;
    UartLinkMonitor* uart_ = nullptr;
    bool up_ = false;
    char line_[384];
    size_t lineLen_ = 0;
};
