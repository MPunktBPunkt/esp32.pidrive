#include "App.h"
#include "core/NetUtil.h"
#include "web/UiPages.h"
#include <WiFi.h>
#include <WiFiManager.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <esp_system.h>

App& App::instance() {
    static App app;
    return app;
}

void App::begin() {
    bootMs_ = millis();
    Serial.begin(115200);
    delay(200);
    Serial.printf("\n=== esp32.pidrive v%s ===\n", FW_VERSION);

    config.begin();
    events.begin();
    menu.begin();
    events.push("boot", FW_VERSION);

    setupWifi();

    if (config.enableMdns) {
        String mdns = "pidrive-" + NetUtil::macNoColon().substring(6);
        if (MDNS.begin(mdns.c_str())) {
            Serial.printf("[mDNS] %s.local\n", mdns.c_str());
            events.push("mdns.up", mdns.c_str());
        }
    }

    setupWeb();
    hub.begin(&config);
    hub.setPayloadBuilder([](JsonDocument& doc) { App::instance().buildHeartbeat(doc); });
    if (config.enableHub) hub.sendNow();

    events.push("ready", NetUtil::localIp().c_str());
}

void App::setupWifi() {
    WiFi.mode(WIFI_STA);
    WiFiManager wm;
    WiFiManagerParameter pName("name", "Geraetename", config.deviceName.c_str(), 32);
    WiFiManagerParameter pHost("hub_host", "ESP-Hub IP", config.hubHost.c_str(), 40);
    WiFiManagerParameter pPort("hub_port", "Port", String(config.hubPort).c_str(), 6);
    wm.addParameter(&pName);
    wm.addParameter(&pHost);
    wm.addParameter(&pPort);
    wm.setConfigPortalTimeout(180);
    String ap = String("pidrive-") + NetUtil::macNoColon().substring(8);
    bool ok = wm.autoConnect(ap.c_str());
    if (pName.getValue()[0]) config.deviceName = pName.getValue();
    if (pHost.getValue()[0]) config.hubHost = pHost.getValue();
    int port = atoi(pPort.getValue());
    if (port > 0) config.hubPort = port;
    config.save();
    if (!ok) {
        events.push("wifi.fail", "portal timeout");
        Serial.println("[WiFi] Portal timeout — SoftAP bleibt ggf. aktiv");
    } else {
        events.push("wifi.up", NetUtil::localIp().c_str());
        Serial.printf("[WiFi] %s\n", NetUtil::localIp().c_str());
    }
}

void App::setupWeb() {
    server_.on("/", HTTP_GET, [this]() { handleRoot(); });
    server_.on("/api/status", HTTP_GET, [this]() { handleApiStatus(); });
    server_.on("/api/menu", HTTP_GET, [this]() { handleApiMenu(); });
    server_.on("/api/events", HTTP_GET, [this]() { handleApiEvents(); });
    server_.on("/api/events", HTTP_DELETE, [this]() { handleApiEventsClear(); });
    server_.on("/api/config", HTTP_GET, [this]() { handleApiConfigGet(); });
    server_.on("/api/config", HTTP_POST, [this]() { handleApiConfigPost(); });
    server_.on("/api/lab/play", HTTP_POST, [this]() { handleApiLabPlay(); });
    server_.on("/api/lab/usb-toggle", HTTP_POST, [this]() { handleApiLabUsbToggle(); });
    server_.on("/api/restart", HTTP_POST, [this]() { handleRestart(); });
    server_.on(
        "/ota-upload", HTTP_POST,
        [this]() {
            server_.sendHeader(F("Connection"), F("close"));
            if (Update.hasError()) {
                events.push("ota.fail", "upload");
                server_.send(500, F("text/plain"), F("OTA fail"));
            } else {
                events.push("ota.done", "upload");
                server_.send(200, F("text/plain"), F("OK"));
                delay(400);
                ESP.restart();
            }
        },
        [this]() { handleOtaUpload(); });
    server_.begin();
    Serial.println("[HTTP] :80");
}

void App::loop() {
    server_.handleClient();
    hub.loop();
}

void App::buildHeartbeat(JsonDocument& doc) {
    doc["mac"] = NetUtil::macNoColon();
    doc["name"] = config.deviceName;
    doc["hwType"] = "esp32";
    doc["chipModel"] = "esp32s3";
    doc["version"] = FW_VERSION;
    doc["fwType"] = FW_TYPE;
    doc["ip"] = NetUtil::localIp();
    doc["rssi"] = WiFi.RSSI();
    doc["uptime"] = (millis() - bootMs_) / 1000UL;
    doc["freeHeap"] = ESP.getFreeHeap();
    doc["freeSketch"] = ESP.getFreeSketchSpace();

    JsonDocument ios;
    ios["usbEnumerated"] = usbEnumerated;
    ios["pumpState"] = pumpUp ? "up" : "down";
    ios["bufferMs"] = bufferMs;
    ios["activeName"] = menu.playingName();
    ios["otaState"] = hub.otaPending() ? "pending" : "idle";
    ios["labMode"] = config.labMode;
    String iosStr;
    serializeJson(ios, iosStr);
    doc["ios"] = iosStr;
}

void App::buildStatus(JsonDocument& doc) {
    doc["ok"] = true;
    doc["version"] = FW_VERSION;
    doc["fwType"] = FW_TYPE;
    doc["name"] = config.deviceName;
    doc["ip"] = NetUtil::localIp();
    doc["mac"] = NetUtil::macNoColon();
    doc["uptime"] = NetUtil::fmtUptime((millis() - bootMs_) / 1000UL);
    doc["freeHeap"] = ESP.getFreeHeap();
    doc["wifiRssi"] = WiFi.RSSI();
    doc["hubOk"] = hub.lastOk();
    doc["usbEnumerated"] = usbEnumerated;
    doc["pumpUp"] = pumpUp;
    doc["bufferMs"] = bufferMs;
    doc["bufferTargetMs"] = config.bufferTargetMs;
    doc["playingUid"] = menu.playingUid();
    doc["playingName"] = menu.playingName();
    doc["labMode"] = config.labMode;
    doc["chipModel"] = NetUtil::chipModel();
}

void App::handleRoot() {
    server_.sendHeader(F("Cache-Control"), F("no-store"));
    server_.send_P(200, "text/html", PAGE_MAIN);
}

void App::handleApiStatus() {
    JsonDocument doc;
    buildStatus(doc);
    NetUtil::sendJson(server_, 200, doc);
}

void App::handleApiMenu() {
    JsonDocument doc;
    doc["ok"] = true;
    JsonObject menuObj = doc["menu"].to<JsonObject>();
    menu.toJson(menuObj);
    NetUtil::sendJson(server_, 200, doc);
}

void App::handleApiEvents() {
    uint32_t since = 0;
    if (server_.hasArg("since")) since = (uint32_t)server_.arg("since").toInt();
    JsonDocument doc;
    doc["ok"] = true;
    doc["nextSeq"] = events.nextSeq();
    JsonArray arr = doc["events"].to<JsonArray>();
    events.toJsonArray(arr, since);
    NetUtil::sendJson(server_, 200, doc);
}

void App::handleApiEventsClear() {
    events.clear();
    events.push("events.clear", "");
    JsonDocument doc;
    doc["ok"] = true;
    NetUtil::sendJson(server_, 200, doc);
}

void App::handleApiConfigGet() {
    JsonDocument doc;
    doc["ok"] = true;
    JsonObject cfg = doc["config"].to<JsonObject>();
    config.toJson(cfg);
    NetUtil::sendJson(server_, 200, doc);
}

void App::handleApiConfigPost() {
    JsonDocument body;
    if (!NetUtil::readJsonBody(server_, body)) return;
    JsonVariantConst cfgIn = body["config"];
    if (!cfgIn.isNull()) {
        config.fromJson(cfgIn);
    } else {
        config.fromJson(body.as<JsonVariantConst>());
    }
    config.save();
    events.push("config.save", config.deviceName.c_str());
    JsonDocument doc;
    doc["ok"] = true;
    JsonObject cfg = doc["config"].to<JsonObject>();
    config.toJson(cfg);
    NetUtil::sendJson(server_, 200, doc);
}

void App::handleApiLabPlay() {
    if (!config.labMode) {
        NetUtil::sendError(server_, 403, "labMode aus");
        return;
    }
    JsonDocument body;
    if (!NetUtil::readJsonBody(server_, body)) return;
    const char* uid = body["uid"] | "";
    if (!menu.playByUid(uid)) {
        NetUtil::sendError(server_, 404, "uid unbekannt");
        return;
    }
    events.push("play.guess", uid);
    events.push("event.sent", uid);
    JsonDocument doc;
    doc["ok"] = true;
    doc["playingUid"] = menu.playingUid();
    doc["playingName"] = menu.playingName();
    NetUtil::sendJson(server_, 200, doc);
}

void App::handleApiLabUsbToggle() {
    if (!config.labMode) {
        NetUtil::sendError(server_, 403, "labMode aus");
        return;
    }
    usbEnumerated = !usbEnumerated;
    events.push(usbEnumerated ? "usb.enumerated" : "usb.gone", "lab-toggle");
    JsonDocument doc;
    doc["ok"] = true;
    doc["usbEnumerated"] = usbEnumerated;
    NetUtil::sendJson(server_, 200, doc);
}

void App::handleOtaUpload() {
    HTTPUpload& upload = server_.upload();
    if (upload.status == UPLOAD_FILE_START) {
        events.push("ota.start", upload.filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) Update.printError(Serial);
    } else if (upload.status == UPLOAD_FILE_END) {
        if (!Update.end(true)) Update.printError(Serial);
    }
}

void App::handleRestart() {
    events.push("restart", "web");
    server_.send(200, F("text/plain"), F("OK"));
    delay(300);
    ESP.restart();
}
