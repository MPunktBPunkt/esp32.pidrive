#include "App.h"
#include "core/NetUtil.h"
#include "web/UiPages.h"
#include <WiFi.h>
#include <WiFiManager.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <esp_system.h>
#include <cstring>

App& App::instance() {
    static App app;
    return app;
}

void App::begin() {
    bootMs_ = millis();
    Serial.begin(115200);
    delay(200);
    Serial.printf("\n=== esp32.pidrive v%s ===\n", FW_VERSION);
    Serial.println("[MODE] TinyUSB OTG MSC + UART Serial (dual-USB board: OTG->car, UART->PC)");

    led.begin();

    config.begin();
    // Leftover NVS name from previous FW on this board
    if (config.deviceName.indexOf("HeartRate") >= 0 || config.deviceName.indexOf("heartrate") >= 0) {
        config.deviceName = DEVICE_NAME_DEFAULT;
        config.save();
    }
    events.begin();
    menu.begin();
    events.push("boot", FW_VERSION);

    // MSC before WiFi so car USB enumerates quickly when bus-powered
    msc.begin(&events, &menu);
    uart.begin(&events);

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
    if (config.enableHub && WiFi.status() == WL_CONNECTED) hub.sendNow();

    char ready[64];
    snprintf(ready, sizeof(ready), "AP %s STA %s", softApIp.c_str(),
             WiFi.status() == WL_CONNECTED ? NetUtil::localIp().c_str() : "-");
    events.push("ready", ready);
}

void App::startSoftAp() {
    softApSsid = String("pidrive-") + NetUtil::macNoColon().substring(6);
    bool ok = WiFi.softAP(softApSsid.c_str(), config.softApPass.c_str());
    softApIp = WiFi.softAPIP().toString();
    if (ok) {
        Serial.printf("[SoftAP] SSID=%s pass=%s IP=%s\n", softApSsid.c_str(), config.softApPass.c_str(),
                      softApIp.c_str());
        events.push("softap.up", softApSsid.c_str());
    } else {
        events.push("softap.fail", "");
    }
}

void App::setupWifi() {
    // Car-standalone: SoftAP so phone reaches WebUI without home WiFi / Pi
    if (config.enableSoftAp && config.enableSta) {
        WiFi.mode(WIFI_AP_STA);
    } else if (config.enableSoftAp) {
        WiFi.mode(WIFI_AP);
    } else {
        WiFi.mode(WIFI_STA);
    }

    if (config.enableSoftAp) startSoftAp();

    if (!config.enableSta) {
        events.push("wifi.sta", "disabled (car SoftAP-only)");
        return;
    }

    WiFiManager wm;
    WiFiManagerParameter pName("name", "Geraetename", config.deviceName.c_str(), 32);
    WiFiManagerParameter pHost("hub_host", "ESP-Hub IP", config.hubHost.c_str(), 40);
    WiFiManagerParameter pPort("hub_port", "Port", String(config.hubPort).c_str(), 6);
    wm.addParameter(&pName);
    wm.addParameter(&pHost);
    wm.addParameter(&pPort);
    wm.setConfigPortalTimeout(90);  // don't block car tests forever
    wm.setHostname(softApSsid.c_str());
    bool ok = wm.autoConnect(softApSsid.c_str(), config.softApPass.c_str());
    if (pName.getValue()[0]) config.deviceName = pName.getValue();
    if (pHost.getValue()[0]) config.hubHost = pHost.getValue();
    int port = atoi(pPort.getValue());
    if (port > 0) config.hubPort = port;
    config.save();

    // WiFiManager may tear SoftAP — restore for car phone access
    if (config.enableSoftAp) {
        WiFi.mode(WIFI_AP_STA);
        startSoftAp();
    }

    if (!ok) {
        events.push("wifi.sta", "timeout — SoftAP remains");
        Serial.println("[WiFi] STA timeout — use SoftAP for WebUI");
    } else {
        events.push("wifi.up", NetUtil::localIp().c_str());
        Serial.printf("[WiFi] STA %s\n", NetUtil::localIp().c_str());
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
    server_.on("/api/metrics", HTTP_GET, [this]() { handleApiMetrics(); });
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
    msc.loop();
    uart.loop();

    // RGB priority: error > OTG mount > UART activity > idle green
    if (!msc.ready()) {
        led.setMode(StatusLed::Mode::Error);
    } else if (msc.plugged()) {
        led.setMode(StatusLed::Mode::OtgMount);
    } else if (uart.linkUp()) {
        led.setMode(StatusLed::Mode::UartLink);
    } else {
        led.setMode(StatusLed::Mode::Idle);
    }
    led.loop();
}

void App::buildHeartbeat(JsonDocument& doc) {
    doc["mac"] = NetUtil::macNoColon();
    doc["name"] = config.deviceName;
    doc["hwType"] = "esp32";
    doc["chipModel"] = "esp32s3";
    doc["version"] = FW_VERSION;
    doc["fwType"] = FW_TYPE;
    doc["ip"] = WiFi.status() == WL_CONNECTED ? NetUtil::localIp() : softApIp;
    doc["rssi"] = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
    doc["uptime"] = (millis() - bootMs_) / 1000UL;
    doc["freeHeap"] = ESP.getFreeHeap();
    doc["freeSketch"] = ESP.getFreeSketchSpace();

    JsonDocument ios;
    ios["usbEnumerated"] = msc.plugged();
    ios["otgUp"] = msc.plugged();
    ios["otgSuspended"] = msc.suspended();
    ios["uartUp"] = uart.linkUp();
    ios["uartState"] = uart.stateName();
    ios["mscReady"] = msc.ready();
    ios["pumpState"] = pumpUp ? "up" : "down";
    ios["bufferMs"] = bufferMs;
    ios["activeName"] = menu.playingName();
    ios["otaState"] = hub.otaPending() ? "pending" : "idle";
    ios["labMode"] = config.labMode;
    ios["softAp"] = softApSsid;
    ios["msPlugToPlay"] = msc.msPlugToPlayGuess();
    String iosStr;
    serializeJson(ios, iosStr);
    doc["ios"] = iosStr;
}

void App::buildStatus(JsonDocument& doc) {
    doc["ok"] = true;
    doc["version"] = FW_VERSION;
    doc["fwType"] = FW_TYPE;
    doc["name"] = config.deviceName;
    doc["ip"] = WiFi.status() == WL_CONNECTED ? NetUtil::localIp() : "";
    doc["softApSsid"] = softApSsid;
    doc["softApIp"] = softApIp;
    doc["softApPass"] = config.softApPass;
    doc["mac"] = NetUtil::macNoColon();
    doc["uptime"] = NetUtil::fmtUptime((millis() - bootMs_) / 1000UL);
    doc["freeHeap"] = ESP.getFreeHeap();
    doc["wifiRssi"] = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
    doc["hubOk"] = hub.lastOk();
    doc["usbEnumerated"] = msc.plugged();
    doc["otgUp"] = msc.plugged();
    doc["otgSuspended"] = msc.suspended();
    doc["uartUp"] = uart.linkUp();
    doc["uartState"] = uart.stateName();
    doc["mscReady"] = msc.ready();
    doc["pumpUp"] = pumpUp;
    doc["bufferMs"] = bufferMs;
    doc["bufferTargetMs"] = config.bufferTargetMs;
    doc["playingUid"] = menu.playingUid();
    doc["playingName"] = menu.playingName();
    doc["labMode"] = config.labMode;
    doc["chipModel"] = NetUtil::chipModel();
    doc["led"] = led.modeName();
    JsonObject m = doc["msc"].to<JsonObject>();
    msc.toJson(m);
    JsonArray tr = doc["mscTrace"].to<JsonArray>();
    msc.traceToJson(tr);
    JsonObject u = doc["uart"].to<JsonObject>();
    uart.toJson(u);
    JsonObject ports = doc["ports"].to<JsonObject>();
    JsonObject otg = ports["otg"].to<JsonObject>();
    otg["label"] = "AUTO";
    otg["role"] = "car-host";
    otg["up"] = msc.plugged();
    otg["suspended"] = msc.suspended();
    otg["msSinceChange"] = msc.msSinceChange();
    otg["plugCount"] = msc.plugCount();
    otg["unplugCount"] = msc.unplugCount();
    otg["sense"] = "tinyusb-mount";
    JsonObject pi = ports["uart"].to<JsonObject>();
    pi["label"] = "PI";
    pi["role"] = "uart-bridge";
    pi["up"] = uart.linkUp();
    pi["state"] = uart.stateName();
    pi["msSinceChange"] = uart.msSinceChange();
    pi["msSinceRx"] = uart.msSinceRx();
    pi["rxBytes"] = uart.rxBytes();
    pi["sense"] = "serial-activity";
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
    if (!cfgIn.isNull()) config.fromJson(cfgIn);
    else config.fromJson(body.as<JsonVariantConst>());
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
    events.push("event.sent", "lab-simulate");
    JsonDocument doc;
    doc["ok"] = true;
    doc["playingUid"] = menu.playingUid();
    doc["playingName"] = menu.playingName();
    NetUtil::sendJson(server_, 200, doc);
}

void App::handleApiMetrics() {
    JsonDocument doc;
    doc["ok"] = true;
    doc["uptimeMs"] = millis() - bootMs_;
    doc["freeHeap"] = ESP.getFreeHeap();
    JsonObject m = doc["msc"].to<JsonObject>();
    msc.toJson(m);
    JsonArray tr = doc["mscTrace"].to<JsonArray>();
    msc.traceToJson(tr);
    JsonObject u = doc["uart"].to<JsonObject>();
    uart.toJson(u);
    doc["otgUp"] = msc.plugged();
    doc["uartUp"] = uart.linkUp();
    doc["playingUid"] = menu.playingUid();
    doc["playingName"] = menu.playingName();
    doc["eventCount"] = events.count();
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
