#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "ConfigStore.h"

class HubClient {
public:
    void begin(ConfigStore* config);
    void loop();
    void sendNow();
    bool lastOk() const { return lastOk_; }
    unsigned long lastSuccessMs() const { return lastSuccess_; }
    void setPayloadBuilder(void (*builder)(JsonDocument& doc));
    bool otaPending() const { return otaPending_; }

private:
    void sendHeartbeat();
    void performOta(const String& url);

    ConfigStore* config_ = nullptr;
    void (*builder_)(JsonDocument& doc) = nullptr;
    unsigned long lastHeartbeat_ = 0;
    unsigned long lastSuccess_ = 0;
    bool lastOk_ = false;
    bool otaPending_ = false;
    String otaUrl_;
};
