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
    playPlugWindowMs = 2500;
    playMinSeqBytes = 6000;
    playHeadLbaSlop = 12;
    playCooldownMs = 5000;
    playPrefetchLbaSlop = 2;
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
    playHeadLbaSlop = prefs.getUChar("play_head", playHeadLbaSlop);
    playCooldownMs = prefs.getUShort("play_cd", playCooldownMs);
    playPrefetchLbaSlop = prefs.getUChar("play_pf", playPrefetchLbaSlop);
    prefs.end();
    if (heartbeatIntervalS < 5) heartbeatIntervalS = 5;
    if (bufferTargetMs < 500) bufferTargetMs = 500;
    if (softApPass.length() < 8) softApPass = "pidrive12";
    if (pumpTcpPort == 0) pumpTcpPort = 9090;
    if (playMinSeqBytes < 512) playMinSeqBytes = 512;
    if (playHeadLbaSlop > 64) playHeadLbaSlop = 64;
    if (playPrefetchLbaSlop > 32) playPrefetchLbaSlop = 32;
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
    prefs.putUChar("play_head", playHeadLbaSlop);
    prefs.putUShort("play_cd", playCooldownMs);
    prefs.putUChar("play_pf", playPrefetchLbaSlop);
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
    obj["playHeadLbaSlop"] = playHeadLbaSlop;
    obj["playCooldownMs"] = playCooldownMs;
    obj["playPrefetchLbaSlop"] = playPrefetchLbaSlop;
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
    if (!obj["playHeadLbaSlop"].isNull()) playHeadLbaSlop = obj["playHeadLbaSlop"].as<uint8_t>();
    if (!obj["playCooldownMs"].isNull()) playCooldownMs = obj["playCooldownMs"].as<uint16_t>();
    if (!obj["playPrefetchLbaSlop"].isNull()) playPrefetchLbaSlop = obj["playPrefetchLbaSlop"].as<uint8_t>();
    if (deviceName.length() == 0) deviceName = DEVICE_NAME_DEFAULT;
    if (hubPort <= 0 || hubPort > 65535) hubPort = HUB_PORT_DEFAULT;
    if (heartbeatIntervalS < 5) heartbeatIntervalS = 5;
    if (bufferTargetMs < 500) bufferTargetMs = 500;
    if (softApPass.length() < 8) softApPass = "pidrive12";
    if (pumpTcpPort == 0) pumpTcpPort = 9090;
    if (playMinSeqBytes < 512) playMinSeqBytes = 512;
    if (playHeadLbaSlop > 64) playHeadLbaSlop = 64;
    if (playPrefetchLbaSlop > 32) playPrefetchLbaSlop = 32;
    return true;
}
