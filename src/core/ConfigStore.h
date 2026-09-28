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
    /** Try STA via WiFiManager; SoftAP bleibt parallel (Lab + Car) */
    bool enableSta = true;
    /** PUMP TCP listener (same framing as UART) — SoftAP/STA, default :9090 */
    bool enablePumpTcp = true;
    uint16_t pumpTcpPort = 9090;

    // Play-Detection (USB-MSC looksLikePlay) — BMW A/B via SoftAP Config
    /** Ignore head reads this long after plug (index window). 0 = off. */
    uint16_t playPlugWindowMs = 500;
    /** Minimum sequential file bytes before play.guess. */
    uint16_t playMinSeqBytes = 6000;
    /** Accept seq start within this many LBAs after file start. */
    uint8_t playHeadLbaSlop = 12;
    /** Suppress further play.guess after first arm. */
    uint16_t playCooldownMs = 5000;
    /** Mid-file prefetch if seq start > fileStart + this. */
    uint8_t playPrefetchLbaSlop = 2;

    void begin();
    void load();
    void save();
    void factoryReset();
    void applyDefaults();
    void toJson(JsonObject obj) const;
    bool fromJson(JsonVariantConst obj);

private:
    static constexpr uint8_t kConfigVersion = 4;
};
