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
    prefs.end();
    if (heartbeatIntervalS < 5) heartbeatIntervalS = 5;
    if (bufferTargetMs < 500) bufferTargetMs = 500;
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
    if (deviceName.length() == 0) deviceName = DEVICE_NAME_DEFAULT;
    if (hubPort <= 0 || hubPort > 65535) hubPort = HUB_PORT_DEFAULT;
    if (heartbeatIntervalS < 5) heartbeatIntervalS = 5;
    if (bufferTargetMs < 500) bufferTargetMs = 500;
    return true;
}
