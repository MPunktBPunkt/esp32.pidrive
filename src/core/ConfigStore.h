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
    /** After indexSettled: action/folder/pump:* (BMW often one 4 KiB head read). */
    uint16_t playNavMinSeqBytes = 4096;
    /** Accept seq start within this many LBAs after file start. */
    uint8_t playHeadLbaSlop = 12;
    /** Suppress further play.guess after first arm. */
    uint16_t playCooldownMs = 5000;
    /** Mid-file prefetch if seq start > fileStart + this. */
    uint8_t playPrefetchLbaSlop = 2;

    // Stall / E7 (STALL-BUILD-SPEC) — defaults keep 0.4.46 behaviour
    bool stallEnable = false;
    uint8_t stallScope = 1;       // 0 off, 1 sequential after head, 2 all live
    uint8_t stallTimer = 0;       // 0=cmd, 1=cb (inactivity)
    uint16_t stallMaxMs = 500;
    uint16_t stallChunkBytes = 4096;
    uint8_t stallRetryMs = 2;
    uint16_t leadBytes = 0;
    bool underrunCursorHold = false;
    bool silenceMatchLive = false;
    /** Experimental selection barrier. Production default MUST remain false. */
    bool selectionBarrierEnable = false;
    /** Hard kill for BUSY while the selected stream is being prepared. */
    uint16_t selectionBarrierMaxMs = 1200;
    /** Audio bytes required in addition to a non-empty ID3 header. */
    uint16_t selectionBarrierLeadBytes = 8192;
    /** FAT volume label override (≤11). Empty = coupled to remountGen (PD%04u). */
    String volumeLabel = "";
    /** FAT BPB Volume-ID override. 0 = follow remountGen (0x5044 | gen). */
    uint32_t fatVolumeId = 0;

    void begin();
    void load();
    void save();
    void factoryReset();
    void applyDefaults();
    void toJson(JsonObject obj) const;
    bool fromJson(JsonVariantConst obj);

private:
    static constexpr uint8_t kConfigVersion = 7;
};
