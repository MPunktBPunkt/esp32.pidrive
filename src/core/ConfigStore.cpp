#include "ConfigStore.h"
#include <Preferences.h>

static Preferences prefs;

void ConfigStore::applyDefaults() {
    deviceName = DEVICE_NAME_DEFAULT;
    hubHost = HUB_HOST_DEFAULT;
    hubPort = HUB_PORT_DEFAULT;
    enableHub = true;
    heartbeatIntervalS = 30;
    enableMdns = true;
    bufferTargetMs = 5000;
    labMode = true;
    timingProfile = 0;
    enableSoftAp = true;
    softApPass = "pidrive12";
    enableSta = true;  // Lab: WLAN wiederfinden nach OTA; SoftAP parallel
    enablePumpTcp = true;
    pumpTcpPort = 9090;
    playPlugWindowMs = 500;
    playMinSeqBytes = 6000;
    playNavMinSeqBytes = 4096;
    playHeadLbaSlop = 12;
    playCooldownMs = 5000;
    playPrefetchLbaSlop = 2;
    stallEnable = false;
    stallScope = 1;
    stallTimer = 0;
    stallMaxMs = 500;
    stallChunkBytes = 4096;
    stallRetryMs = 2;
    leadBytes = 0;
    underrunCursorHold = false;
    silenceMatchLive = false;
    selectionBarrierEnable = false;
    selectionBarrierMaxMs = 1200;
    selectionBarrierLeadBytes = 8192;
    volumeLabel = "";
    fatVolumeId = 0;
}

void ConfigStore::begin() {
    applyDefaults();
    load();
}

void ConfigStore::load() {
    prefs.begin("esphub", true);
    deviceName = prefs.getString("name", deviceName);
    hubHost = prefs.getString("hub_host", hubHost);
    hubPort = prefs.getInt("hub_port", hubPort);
    prefs.end();

    prefs.begin("pidrive", true);
    uint8_t ver = prefs.getUChar("cfg_ver", 0);
    if (ver == 0) {
        prefs.end();
        return;
    }
    enableHub = prefs.getBool("en_hub", enableHub);
    heartbeatIntervalS = prefs.getUShort("hb_s", heartbeatIntervalS);
    enableMdns = prefs.getBool("en_mdns", enableMdns);
    bufferTargetMs = prefs.getUShort("buf_ms", bufferTargetMs);
    labMode = prefs.getBool("lab", labMode);
    timingProfile = prefs.getUInt("tprof", timingProfile);
    enableSoftAp = prefs.getBool("softap", enableSoftAp);
    softApPass = prefs.getString("ap_pass", softApPass);
    enableSta = prefs.getBool("en_sta", enableSta);
    enablePumpTcp = prefs.getBool("en_ptcp", enablePumpTcp);
    pumpTcpPort = prefs.getUShort("ptcp_port", pumpTcpPort);
    playPlugWindowMs = prefs.getUShort("play_plug", playPlugWindowMs);
    playMinSeqBytes = prefs.getUShort("play_seq", playMinSeqBytes);
    playNavMinSeqBytes = prefs.getUShort("play_nav", playNavMinSeqBytes);
    playHeadLbaSlop = prefs.getUChar("play_head", playHeadLbaSlop);
    playCooldownMs = prefs.getUShort("play_cd", playCooldownMs);
    playPrefetchLbaSlop = prefs.getUChar("play_pf", playPrefetchLbaSlop);
    if (ver >= 5) {
        stallEnable = prefs.getBool("stall_en", stallEnable);
        stallScope = prefs.getUChar("stall_sc", stallScope);
        stallTimer = prefs.getUChar("stall_tm", stallTimer);
        stallMaxMs = prefs.getUShort("stall_ms", stallMaxMs);
        stallChunkBytes = prefs.getUShort("stall_ch", stallChunkBytes);
        stallRetryMs = prefs.getUChar("stall_rt", stallRetryMs);
        leadBytes = prefs.getUShort("lead_b", leadBytes);
        underrunCursorHold = prefs.getBool("stall_hold", underrunCursorHold);
        silenceMatchLive = prefs.getBool("sil_live", silenceMatchLive);
        volumeLabel = prefs.getString("vol_lab", volumeLabel);
        if (ver >= 6) {
            fatVolumeId = prefs.getUInt("fat_vid", fatVolumeId);
        }
        if (ver >= 7) {
            selectionBarrierEnable = prefs.getBool("selbar_en", selectionBarrierEnable);
            selectionBarrierMaxMs = prefs.getUShort("selbar_ms", selectionBarrierMaxMs);
            selectionBarrierLeadBytes = prefs.getUShort("selbar_lead", selectionBarrierLeadBytes);
        }
    }
    prefs.end();
    if (heartbeatIntervalS < 5) heartbeatIntervalS = 5;
    if (bufferTargetMs < 500) bufferTargetMs = 500;
    if (softApPass.length() < 8) softApPass = "pidrive12";
    if (pumpTcpPort == 0) pumpTcpPort = 9090;
    if (playMinSeqBytes < 512) playMinSeqBytes = 512;
    if (playNavMinSeqBytes < 512) playNavMinSeqBytes = 512;
    if (playHeadLbaSlop > 64) playHeadLbaSlop = 64;
    if (playPrefetchLbaSlop > 32) playPrefetchLbaSlop = 32;
    if (stallMaxMs > 15000) stallMaxMs = 15000;
    if (stallChunkBytes != 512 && stallChunkBytes != 1024 && stallChunkBytes != 2048 &&
        stallChunkBytes != 4096) {
        stallChunkBytes = 4096;
    }
    if (stallRetryMs < 1) stallRetryMs = 1;
    if (stallRetryMs > 20) stallRetryMs = 20;
    if (stallScope > 2) stallScope = 1;
    if (stallTimer > 1) stallTimer = 0;
    if (volumeLabel.length() > 11) volumeLabel = volumeLabel.substring(0, 11);
    if (selectionBarrierMaxMs < 50) selectionBarrierMaxMs = 50;
    if (selectionBarrierMaxMs > 5000) selectionBarrierMaxMs = 5000;
    if (selectionBarrierLeadBytes > 32768) selectionBarrierLeadBytes = 32768;
}

void ConfigStore::save() {
    prefs.begin("esphub", false);
    prefs.putString("name", deviceName);
    prefs.putString("hub_host", hubHost);
    prefs.putInt("hub_port", hubPort);
    prefs.end();

    prefs.begin("pidrive", false);
    prefs.putUChar("cfg_ver", kConfigVersion);
    prefs.putBool("en_hub", enableHub);
    prefs.putUShort("hb_s", heartbeatIntervalS);
    prefs.putBool("en_mdns", enableMdns);
    prefs.putUShort("buf_ms", bufferTargetMs);
    prefs.putBool("lab", labMode);
    prefs.putUInt("tprof", timingProfile);
    prefs.putBool("softap", enableSoftAp);
    prefs.putString("ap_pass", softApPass);
    prefs.putBool("en_sta", enableSta);
    prefs.putBool("en_ptcp", enablePumpTcp);
    prefs.putUShort("ptcp_port", pumpTcpPort);
    prefs.putUShort("play_plug", playPlugWindowMs);
    prefs.putUShort("play_seq", playMinSeqBytes);
    prefs.putUShort("play_nav", playNavMinSeqBytes);
    prefs.putUChar("play_head", playHeadLbaSlop);
    prefs.putUShort("play_cd", playCooldownMs);
    prefs.putUChar("play_pf", playPrefetchLbaSlop);
    prefs.putBool("stall_en", stallEnable);
    prefs.putUChar("stall_sc", stallScope);
    prefs.putUChar("stall_tm", stallTimer);
    prefs.putUShort("stall_ms", stallMaxMs);
    prefs.putUShort("stall_ch", stallChunkBytes);
    prefs.putUChar("stall_rt", stallRetryMs);
    prefs.putUShort("lead_b", leadBytes);
    prefs.putBool("stall_hold", underrunCursorHold);
    prefs.putBool("sil_live", silenceMatchLive);
    prefs.putString("vol_lab", volumeLabel);
    prefs.putUInt("fat_vid", fatVolumeId);
    prefs.putBool("selbar_en", selectionBarrierEnable);
    prefs.putUShort("selbar_ms", selectionBarrierMaxMs);
    prefs.putUShort("selbar_lead", selectionBarrierLeadBytes);
    prefs.end();
}

void ConfigStore::factoryReset() {
    prefs.begin("pidrive", false);
    prefs.clear();
    prefs.end();
    applyDefaults();
    save();
}

void ConfigStore::toJson(JsonObject obj) const {
    obj["deviceName"] = deviceName;
    obj["hubHost"] = hubHost;
    obj["hubPort"] = hubPort;
    obj["enableHub"] = enableHub;
    obj["heartbeatIntervalS"] = heartbeatIntervalS;
    obj["enableMdns"] = enableMdns;
    obj["bufferTargetMs"] = bufferTargetMs;
    obj["labMode"] = labMode;
    obj["timingProfile"] = timingProfile;
    obj["enableSoftAp"] = enableSoftAp;
    obj["softApPass"] = softApPass;
    obj["enableSta"] = enableSta;
    obj["enablePumpTcp"] = enablePumpTcp;
    obj["pumpTcpPort"] = pumpTcpPort;
    obj["playPlugWindowMs"] = playPlugWindowMs;
    obj["playMinSeqBytes"] = playMinSeqBytes;
    obj["playNavMinSeqBytes"] = playNavMinSeqBytes;
    obj["playHeadLbaSlop"] = playHeadLbaSlop;
    obj["playCooldownMs"] = playCooldownMs;
    obj["playPrefetchLbaSlop"] = playPrefetchLbaSlop;
    obj["stallEnable"] = stallEnable;
    obj["stallScope"] = stallScope;
    obj["stallTimer"] = stallTimer;
    obj["stallMaxMs"] = stallMaxMs;
    obj["stallChunkBytes"] = stallChunkBytes;
    obj["stallRetryMs"] = stallRetryMs;
    obj["leadBytes"] = leadBytes;
    obj["underrunCursorHold"] = underrunCursorHold;
    obj["silenceMatchLive"] = silenceMatchLive;
    obj["volumeLabel"] = volumeLabel;
    obj["fatVolumeId"] = fatVolumeId;
    obj["selectionBarrierEnable"] = selectionBarrierEnable;
    obj["selectionBarrierMaxMs"] = selectionBarrierMaxMs;
    obj["selectionBarrierLeadBytes"] = selectionBarrierLeadBytes;
}

bool ConfigStore::fromJson(JsonVariantConst obj) {
    if (obj["deviceName"].is<const char*>()) deviceName = obj["deviceName"].as<String>();
    if (obj["hubHost"].is<const char*>()) hubHost = obj["hubHost"].as<String>();
    if (!obj["hubPort"].isNull()) hubPort = obj["hubPort"].as<int>();
    if (!obj["enableHub"].isNull()) enableHub = obj["enableHub"].as<bool>();
    if (!obj["heartbeatIntervalS"].isNull()) heartbeatIntervalS = obj["heartbeatIntervalS"].as<uint16_t>();
    if (!obj["enableMdns"].isNull()) enableMdns = obj["enableMdns"].as<bool>();
    if (!obj["bufferTargetMs"].isNull()) bufferTargetMs = obj["bufferTargetMs"].as<uint16_t>();
    if (!obj["labMode"].isNull()) labMode = obj["labMode"].as<bool>();
    if (!obj["timingProfile"].isNull()) timingProfile = obj["timingProfile"].as<uint32_t>();
    if (!obj["enableSoftAp"].isNull()) enableSoftAp = obj["enableSoftAp"].as<bool>();
    if (obj["softApPass"].is<const char*>()) softApPass = obj["softApPass"].as<String>();
    if (!obj["enableSta"].isNull()) enableSta = obj["enableSta"].as<bool>();
    if (!obj["enablePumpTcp"].isNull()) enablePumpTcp = obj["enablePumpTcp"].as<bool>();
    if (!obj["pumpTcpPort"].isNull()) pumpTcpPort = obj["pumpTcpPort"].as<uint16_t>();
    if (!obj["playPlugWindowMs"].isNull()) playPlugWindowMs = obj["playPlugWindowMs"].as<uint16_t>();
    if (!obj["playMinSeqBytes"].isNull()) playMinSeqBytes = obj["playMinSeqBytes"].as<uint16_t>();
    if (!obj["playNavMinSeqBytes"].isNull()) playNavMinSeqBytes = obj["playNavMinSeqBytes"].as<uint16_t>();
    if (!obj["playHeadLbaSlop"].isNull()) playHeadLbaSlop = obj["playHeadLbaSlop"].as<uint8_t>();
    if (!obj["playCooldownMs"].isNull()) playCooldownMs = obj["playCooldownMs"].as<uint16_t>();
    if (!obj["playPrefetchLbaSlop"].isNull()) playPrefetchLbaSlop = obj["playPrefetchLbaSlop"].as<uint8_t>();
    if (!obj["stallEnable"].isNull()) stallEnable = obj["stallEnable"].as<bool>();
    if (!obj["stallScope"].isNull()) stallScope = obj["stallScope"].as<uint8_t>();
    if (!obj["stallTimer"].isNull()) stallTimer = obj["stallTimer"].as<uint8_t>();
    if (!obj["stallMaxMs"].isNull()) stallMaxMs = obj["stallMaxMs"].as<uint16_t>();
    if (!obj["stallChunkBytes"].isNull()) stallChunkBytes = obj["stallChunkBytes"].as<uint16_t>();
    if (!obj["stallRetryMs"].isNull()) stallRetryMs = obj["stallRetryMs"].as<uint8_t>();
    if (!obj["leadBytes"].isNull()) leadBytes = obj["leadBytes"].as<uint16_t>();
    if (!obj["underrunCursorHold"].isNull()) underrunCursorHold = obj["underrunCursorHold"].as<bool>();
    if (!obj["silenceMatchLive"].isNull()) silenceMatchLive = obj["silenceMatchLive"].as<bool>();
    if (obj["volumeLabel"].is<const char*>()) volumeLabel = obj["volumeLabel"].as<String>();
    if (!obj["fatVolumeId"].isNull()) fatVolumeId = obj["fatVolumeId"].as<uint32_t>();
    if (!obj["selectionBarrierEnable"].isNull())
        selectionBarrierEnable = obj["selectionBarrierEnable"].as<bool>();
    if (!obj["selectionBarrierMaxMs"].isNull())
        selectionBarrierMaxMs = obj["selectionBarrierMaxMs"].as<uint16_t>();
    if (!obj["selectionBarrierLeadBytes"].isNull())
        selectionBarrierLeadBytes = obj["selectionBarrierLeadBytes"].as<uint16_t>();
    if (deviceName.length() == 0) deviceName = DEVICE_NAME_DEFAULT;
    if (hubPort <= 0 || hubPort > 65535) hubPort = HUB_PORT_DEFAULT;
    if (heartbeatIntervalS < 5) heartbeatIntervalS = 5;
    if (bufferTargetMs < 500) bufferTargetMs = 500;
    if (softApPass.length() < 8) softApPass = "pidrive12";
    if (pumpTcpPort == 0) pumpTcpPort = 9090;
    if (playMinSeqBytes < 512) playMinSeqBytes = 512;
    if (playNavMinSeqBytes < 512) playNavMinSeqBytes = 512;
    if (playHeadLbaSlop > 64) playHeadLbaSlop = 64;
    if (playPrefetchLbaSlop > 32) playPrefetchLbaSlop = 32;
    if (stallMaxMs > 15000) stallMaxMs = 15000;
    if (stallChunkBytes != 512 && stallChunkBytes != 1024 && stallChunkBytes != 2048 &&
        stallChunkBytes != 4096) {
        stallChunkBytes = 4096;
    }
    if (stallRetryMs < 1) stallRetryMs = 1;
    if (stallRetryMs > 20) stallRetryMs = 20;
    if (stallScope > 2) stallScope = 1;
    if (stallTimer > 1) stallTimer = 0;
    if (volumeLabel.length() > 11) volumeLabel = volumeLabel.substring(0, 11);
    if (selectionBarrierMaxMs < 50) selectionBarrierMaxMs = 50;
    if (selectionBarrierMaxMs > 5000) selectionBarrierMaxMs = 5000;
    if (selectionBarrierLeadBytes > 32768) selectionBarrierLeadBytes = 32768;
    return true;
}
