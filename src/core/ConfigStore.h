#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "BuildFlags.h"

class ConfigStore {
public:
    String deviceName = DEVICE_NAME_DEFAULT;
    String hubHost = HUB_HOST_DEFAULT;
    int hubPort = HUB_PORT_DEFAULT;
    bool enableHub = true;
    uint16_t heartbeatIntervalS = 30;
    bool enableMdns = true;
    uint16_t bufferTargetMs = 5000;
    bool labMode = true;
    uint32_t timingProfile = 0;
    /** SoftAP always on — phone WebUI in car without home WiFi / without Pi */
    bool enableSoftAp = true;
    String softApPass = "pidrive12";  // min 8 chars
    /** Try STA via WiFiManager; false = SoftAP-only (default for car-only tests) */
    bool enableSta = false;

    void begin();
    void load();
    void save();
    void factoryReset();
    void applyDefaults();
    void toJson(JsonObject obj) const;
    bool fromJson(JsonVariantConst obj);

private:
    static constexpr uint8_t kConfigVersion = 2;
};
