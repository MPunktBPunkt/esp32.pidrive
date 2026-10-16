#include "App.h"
#include "core/NetUtil.h"
#include "msc/LabBodySeed.h"
#include "msc/MessCapture.h"
#include "web/UiPages.h"
#include <WiFi.h>
#include <WiFiManager.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <esp_system.h>
#include <cstdlib>
#include <cstring>

static const char* resetReasonName(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_POWERON: return "power_on";
        case ESP_RST_EXT: return "external";
        case ESP_RST_SW: return "software";
        case ESP_RST_PANIC: return "panic";
        case ESP_RST_INT_WDT: return "interrupt_watchdog";
        case ESP_RST_TASK_WDT: return "task_watchdog";
        case ESP_RST_WDT: return "watchdog";
        case ESP_RST_DEEPSLEEP: return "deep_sleep";
        case ESP_RST_BROWNOUT: return "brownout";
        case ESP_RST_SDIO: return "sdio";
        default: return "unknown";
    }
}

App& App::instance() {
    static App app;
    return app;
}

void App::begin() {
    bootMs_ = millis();
    Serial.begin(115200);
    delay(200);
    Serial.printf("\n=== esp32.pidrive v%s ===\n", FW_VERSION);
    Serial.println("[MODE] TinyUSB OTG MSC + UART/TCP PUMP (OTG->car, UART or WLAN->Pi)");

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
    msc.setStreamBuffer(&stream);
    applyPlayDetectFromConfig();
    applyStallFromConfig();
    // Apply last-known / demo names; present immediately if restored from NVS.
    msc.applyMenuSlots(menu);
    if (menu.fromNvs()) {
        msc.presentMedia("nvs");
    }
    uart.begin(&events);
    pump.begin(&events, &menu, &msc, &uart, &stream);
    msc.setPlayHandler([](const char* uid) { App::instance().pump.sendPlayUid(uid); });
    msc.setDiagHandler([](const char* code, const char* detail) {
        if (!MessCapture::instance().captureActive())
            App::instance().pump.sendDiag(code, detail);
    });
    msc.setReadsHandler([](const MscReadBurst& b) {
        if (!MessCapture::instance().captureActive()) App::instance().pump.sendMscReads(b);
    });
    {
        auto& mess = MessCapture::instance();
#if defined(PIDRIVE_MESS) && PIDRIVE_MESS
        mess.setEnabled(true);
#else
        mess.setEnabled(false);
#endif
        mess.setEmitHandler(
            [](const MessCmd& c) { App::instance().pump.sendMessCmd(c); });
    }

    setupWifi();

    if (config.enablePumpTcp) {
        pump.startTcp(config.pumpTcpPort);
    }

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
    server_.on("/api/lab/stop", HTTP_POST, [this]() { handleApiLabStop(); });
    server_.on("/api/lab/remount", HTTP_POST, [this]() { handleApiLabRemount(); });
    server_.on("/api/lab/stream", HTTP_GET, [this]() { handleApiLabStream(); });
    server_.on("/api/lab/overlay_read", HTTP_GET, [this]() { handleApiLabOverlayRead(); });
    server_.on("/api/lab/body_seed", HTTP_POST, [this]() { handleApiLabBodySeed(); });
    server_.on("/api/lab/body_read", HTTP_GET, [this]() { handleApiLabBodyRead(); });
    server_.on("/api/lab/listen", HTTP_GET, [this]() { handleApiLabListen(); });
    server_.on("/api/lab/cover", HTTP_GET, [this]() { handleApiLabCover(); });
    server_.on("/api/lab/usb", HTTP_POST, [this]() { handleApiLabUsb(); });
    server_.on("/api/lab/mess", HTTP_GET, [this]() { handleApiLabMess(); });
    server_.on("/api/lab/mess", HTTP_POST, [this]() { handleApiLabMess(); });
    server_.on("/api/lab/situation", HTTP_POST, [this]() { handleApiLabSituation(); });
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
    serviceCaptureWifiOff();
    server_.handleClient();
    hub.loop();
    msc.loop();
    MessCapture::instance().loop();
    uart.loop();
    pump.loop();
    pumpUp = pump.up();

    // RGB priority: error > OTG mount > UART activity > idle green
    if (!msc.ready()) {
        led.setMode(StatusLed::Mode::Error);
    } else if (msc.plugged()) {
        led.setMode(StatusLed::Mode::OtgMount);
    } else if (uart.linkUp() || pump.up() || pump.tcpClientUp()) {
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
    doc["uptimeMs"] = millis() - bootMs_;
    doc["resetReason"] = resetReasonName(esp_reset_reason());
    doc["resetReasonCode"] = (int)esp_reset_reason();
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
    ios["liveAvailBytes"] = stream.liveAvailBytes();
    ios["liveAvailMs"] = stream.liveAvailMs();
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
    doc["uptimeMs"] = millis() - bootMs_;
    doc["resetReason"] = resetReasonName(esp_reset_reason());
    doc["resetReasonCode"] = (int)esp_reset_reason();
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
    doc["pumpTcp"] = pump.tcpListening();
    doc["pumpTcpPort"] = (int)pump.tcpPort();
    doc["pumpTcpUp"] = pump.tcpClientUp();
    doc["pumpTcpPeer"] = pump.tcpClientIp();
    doc["liveAvailBytes"] = stream.liveAvailBytes();
    doc["liveAvailMs"] = stream.liveAvailMs();
    doc["bufferTargetMs"] = config.bufferTargetMs;
    doc["playingUid"] = menu.playingUid();
    doc["playingName"] = menu.playingName();
    doc["menuRev"] = menu.rev();
    doc["menuCount"] = (int)menu.count();
    {
        JsonObject cover = doc["cover"].to<JsonObject>();
        cover["src"] = pump.coverSrc();
        cover["path"] = pump.coverPath();
        cover["try"] = pump.coverTry();
        cover["folder"] = "assets/usb-msc-covers/";
        cover["default"] = "default.jpg";
    }
    if (stream.active()) {
        JsonObject s = doc["stream"].to<JsonObject>();
        stream.toJson(s);
    }
    doc["labMode"] = config.labMode;
    doc["chipModel"] = NetUtil::chipModel();
    doc["led"] = led.modeName();
    JsonObject m = doc["msc"].to<JsonObject>();
    msc.toJson(m);
    JsonObject mess = doc["mess"].to<JsonObject>();
    MessCapture::instance().toJson(mess);
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
    otg["softUsbAttached"] = MessCapture::instance().usbAttached();
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
    JsonObject tcp = ports["pumpTcp"].to<JsonObject>();
    tcp["label"] = "PUMP-TCP";
    tcp["role"] = "wlan-bridge";
    tcp["port"] = (int)pump.tcpPort();
    tcp["listening"] = pump.tcpListening();
    tcp["up"] = pump.tcpClientUp();
    tcp["peer"] = pump.tcpClientIp();
    tcp["sense"] = "tcp-client";
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
    applyPlayDetectFromConfig();
    applyStallFromConfig();
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
    if (!uid[0]) {
        NetUtil::sendError(server_, 400, "uid fehlt");
        return;
    }
    // SoftAP kann nach Navigation noch alte Buttons zeigen — trotzdem an PUMP
    // weiterleiten (kein 404). playByUid setzt playing-Markierung wenn bekannt.
    const bool known = menu.playByUid(uid);
    if (!known) {
        menu.setPlaying(uid);
    }
    events.push("play.guess", uid);
    pump.sendPlayUid(uid);
    events.push("event.sent", pump.up() ? "pump" : "lab-simulate");
    JsonDocument doc;
    doc["ok"] = true;
    doc["playingUid"] = menu.playingUid();
    doc["playingName"] = menu.playingName();
    doc["known"] = known;
    doc["pumpUp"] = pump.up();
    NetUtil::sendJson(server_, 200, doc);
}

void App::handleApiLabStop() {
    if (!config.labMode) {
        NetUtil::sendError(server_, 403, "labMode aus");
        return;
    }
    menu.clearPlaying();
    events.push("lab.stop", "remote");
    pump.sendPlayUid("pump:stop");
    JsonDocument doc;
    doc["ok"] = true;
    doc["pumpUp"] = pump.up();
    NetUtil::sendJson(server_, 200, doc);
}

void App::handleApiLabRemount() {
    if (!config.labMode) {
        NetUtil::sendError(server_, 403, "labMode aus");
        return;
    }
    if (captureWifiOff_ || captureWifiOffAtMs_) {
        NetUtil::sendError(server_, 409, "capture control owns offline window");
        return;
    }
    // Optional JSON body — empty POST remains valid (legacy clients).
    bool keepId = false;
    String plain = server_.arg("plain");
    if (plain.length() > 0) {
        JsonDocument body;
        if (!deserializeJson(body, plain)) {
            keepId = body["keepIdentity"] | false;
            if (body["volumeLabel"].is<const char*>()) {
                config.volumeLabel = body["volumeLabel"].as<String>();
                if (config.volumeLabel.length() > 11) {
                    config.volumeLabel = config.volumeLabel.substring(0, 11);
                }
            }
            if (!body["fatVolumeId"].isNull()) {
                config.fatVolumeId = body["fatVolumeId"].as<uint32_t>();
            }
            config.save();
            applyStallFromConfig();
        }
    }
    if (keepId) msc.remountMediaKeepIdentity("lab");
    else msc.remountMedia("lab");
    events.push("lab.remount", keepId ? "keepId" : "softap");
    JsonDocument doc;
    doc["ok"] = true;
    doc["otg"] = msc.plugged();
    doc["keepIdentity"] = keepId;
    NetUtil::sendJson(server_, 200, doc);
}

void App::handleApiLabStream() {
    if (!stream.active() || (stream.size() == 0 && stream.id3Len() == 0)) {
        server_.send(204, F("text/plain"), F(""));
        return;
    }
    static uint8_t buf[12288];
    size_t n = stream.copyId3AndAudio(buf, sizeof(buf));
    server_.sendHeader(F("Cache-Control"), F("no-store"));
    server_.sendHeader(F("X-Stream-Uid"), stream.uid());
    server_.sendHeader(F("X-Stream-Size"), String((unsigned)stream.size()));
    server_.sendHeader(F("X-Stream-Id3"), String((unsigned)stream.id3Len()));
    server_.sendHeader(F("X-Stream-AbsEnd"), String(stream.absEnd()));
    server_.setContentLength(n);
    server_.send(200, F("audio/mpeg"), "");
    if (n) server_.client().write(buf, n);
}

/** Q3b Prefill: arm/clear deterministic body behind frozen scan head.
 *  POST {"slot":0,"tag":"A"|"B"|"off","fromOff":348160} */
void App::handleApiLabBodySeed() {
    if (!config.labMode) {
        NetUtil::sendError(server_, 403, "labMode aus");
        return;
    }
    JsonDocument body;
    if (!NetUtil::readJsonBody(server_, body)) return;
    const char* tag = body["tag"] | "";
    int slot = body["slot"] | 0;
    uint32_t fromOff = body["fromOff"] | LabBodySeed::kDefaultFromOff;
    bool ok = false;
    if (!tag[0] || strcmp(tag, "off") == 0) {
        msc.clearLabBodySeed();
        ok = true;
    } else if ((tag[0] == 'A' || tag[0] == 'B') && tag[1] == 0) {
        // Body seed must not compete with live overlay on the same slot.
        if (stream.active()) {
            stream.stop();
            msc.stopStream();
            menu.clearPlaying();
        }
        ok = msc.setLabBodySeed(slot, tag[0], fromOff);
    }
    if (!ok) {
        NetUtil::sendError(server_, 400, "tag A|B|off, slot 0..2");
        return;
    }
    events.push("lab.body_seed", tag[0] ? tag : "off");
    JsonDocument doc;
    doc["ok"] = true;
    doc["active"] = msc.labBodySeedActive();
    doc["slot"] = msc.labBodySeedSlot();
    doc["tag"] = msc.labBodySeedActive() ? String(msc.labBodySeedTag()) : "";
    doc["fromOff"] = msc.labBodySeedFromOff();
    doc["bytesServed"] = msc.seedBytesServed();
    NetUtil::sendJson(server_, 200, doc);
}

/** Q3b Oracle A: SoftAP view of slot bytes (same fill as MSC onRead non-live).
 *  GET /api/lab/body_read?slot=0&off=348160&n=8192 */
void App::handleApiLabBodyRead() {
    if (!config.labMode) {
        NetUtil::sendError(server_, 403, "labMode aus");
        return;
    }
    int slot = server_.hasArg("slot") ? server_.arg("slot").toInt() : 0;
    uint32_t off = server_.hasArg("off") ? (uint32_t)server_.arg("off").toInt() : 0;
    uint32_t nReq = server_.hasArg("n") ? (uint32_t)server_.arg("n").toInt() : 4096;
    if (nReq == 0) nReq = 4096;
    if (nReq > 8192) nReq = 8192;
    static uint8_t buf[8192];
    size_t n = msc.labBodyRead(slot, off, buf, (size_t)nReq);
    server_.sendHeader(F("Cache-Control"), F("no-store"));
    server_.sendHeader(F("X-Body-Slot"), String(slot));
    server_.sendHeader(F("X-File-Off"), String(off));
    server_.sendHeader(F("X-Body-Seed-Active"), msc.labBodySeedActive() ? F("1") : F("0"));
    server_.sendHeader(F("X-Body-Seed-Tag"),
                       msc.labBodySeedActive() ? String(msc.labBodySeedTag()) : String(""));
    server_.sendHeader(F("X-Body-Seed-FromOff"), String(msc.labBodySeedFromOff()));
    server_.setContentLength(n);
    server_.send(200, F("application/octet-stream"), "");
    if (n) server_.client().write(buf, n);
}

/** B4 lab probe: SoftAP readAt(fileOff) without MSC host.
 *  GET /api/lab/overlay_read?off=0&n=4096 → raw bytes + X- headers. */
void App::handleApiLabOverlayRead() {
    if (!config.labMode) {
        NetUtil::sendError(server_, 403, "labMode aus");
        return;
    }
    if (!stream.active()) {
        server_.send(204, F("text/plain"), F(""));
        return;
    }
    uint32_t off = 0;
    uint32_t nReq = 4096;
    if (server_.hasArg("off")) off = (uint32_t)server_.arg("off").toInt();
    if (server_.hasArg("n")) nReq = (uint32_t)server_.arg("n").toInt();
    if (nReq == 0) nReq = 4096;
    if (nReq > 8192) nReq = 8192;
    static uint8_t buf[8192];
    uint32_t ud0 = stream.underruns();
    uint32_t rs0 = stream.headResyncs();
    size_t n = stream.readAt(off, buf, (size_t)nReq);
    uint32_t ud1 = stream.underruns();
    uint32_t rs1 = stream.headResyncs();
    server_.sendHeader(F("Cache-Control"), F("no-store"));
    server_.sendHeader(F("X-Stream-Uid"), stream.uid());
    server_.sendHeader(F("X-File-Off"), String(off));
    server_.sendHeader(F("X-Stream-Id3"), String((unsigned)stream.id3Len()));
    server_.sendHeader(F("X-Stream-AbsBase"), String(stream.absBase()));
    server_.sendHeader(F("X-Stream-AbsEnd"), String(stream.absEnd()));
    server_.sendHeader(F("X-Stream-Size"), String((unsigned)stream.size()));
    server_.sendHeader(F("X-Underruns-Delta"), String(ud1 - ud0));
    server_.sendHeader(F("X-Head-Resyncs-Delta"), String(rs1 - rs0));
    server_.sendHeader(F("X-Head-Resyncs"), String(rs1));
    server_.setContentLength(n);
    server_.send(200, F("application/octet-stream"), "");
    if (n) server_.client().write(buf, n);
}

void App::handleApiLabCover() {
    static uint8_t jpeg[StreamBuffer::kId3Max];
    size_t n = stream.extractApicJpeg(jpeg, sizeof(jpeg));
    if (!n) {
        server_.send(204, F("text/plain"), F(""));
        return;
    }
    server_.sendHeader(F("Cache-Control"), F("no-store"));
    server_.sendHeader(F("X-Stream-Uid"), stream.uid());
    server_.sendHeader(F("X-Cover-Bytes"), String((unsigned)n));
    server_.setContentLength(n);
    server_.send(200, F("image/jpeg"), "");
    server_.client().write(jpeg, n);
}

bool App::requireLabMode() {
    if (!config.labMode) {
        NetUtil::sendError(server_, 403, "labMode aus");
        return false;
    }
    return true;
}

/** Soft USB bus attach/detach (D+ pull-up via TinyUSB). NOT VBUS / physical OTG power.
 *  POST {"attach":true|false, "holdMs":0..5000}  holdMs>0 auto-reattach after detach. */
void App::handleApiLabUsb() {
    if (!requireLabMode()) return;
    if (captureWifiOff_ || captureWifiOffAtMs_) {
        NetUtil::sendError(server_, 409, "capture control owns offline window");
        return;
    }
    JsonDocument body;
    if (!NetUtil::readJsonBody(server_, body)) return;
    auto& mess = MessCapture::instance();
    bool ok = true;
    const char* action = "noop";
    if (!body["attach"].isNull()) {
        bool attach = body["attach"] | true;
        uint16_t holdMs = (uint16_t)(body["holdMs"] | 0);
        if (attach) {
            ok = mess.usbAttach();
            action = "attach";
        } else {
            if (holdMs > 5000) holdMs = 5000;
            msc.armSoftUsbKeepIdentity();
            ok = mess.usbDetach(holdMs);
            action = holdMs ? "cycle" : "detach";
        }
    } else if (!body["cycleMs"].isNull()) {
        uint16_t holdMs = (uint16_t)(body["cycleMs"] | 600);
        if (holdMs < 50) holdMs = 50;
        if (holdMs > 5000) holdMs = 5000;
        msc.armSoftUsbKeepIdentity();
        ok = mess.usbDetach(holdMs);
        action = "cycle";
    } else {
        NetUtil::sendError(server_, 400, "attach bool or cycleMs");
        return;
    }
    events.push("lab.usb", action);
    mess.mark(action);
    JsonDocument doc;
    doc["ok"] = ok;
    doc["action"] = action;
    doc["usbAttached"] = mess.usbAttached();
    doc["otgPlugged"] = msc.plugged();
    doc["note"] = "soft_usb=D+_pullup_not_VBUS";
    NetUtil::sendJson(server_, ok ? 200 : 500, doc);
}

/** GET: health/replay. POST: {"mode":"ram_local"|"ram_wifi_off","wifiOffMs":...} or
 *  {"stop":true,"reason":"..."}. Active RAM capture suppresses command HTTP output. */
void App::handleApiLabMess() {
    if (!requireLabMode()) return;
    auto& mess = MessCapture::instance();
    if (server_.method() == HTTP_POST) {
        JsonDocument body;
        if (!NetUtil::readJsonBody(server_, body)) return;
        if (!body["enable"].isNull()) mess.setEnabled(body["enable"] | true);
        // Reset must happen before starting a new capture; otherwise reset()
        // immediately tears down the capture that was just armed.
        if (body["reset"] | false) mess.reset();
        const char* mode = body["mode"] | "";
        if (strcmp(mode, "ram_local") == 0) {
            if (!mess.beginCapture(MessCaptureMode::RamLocal)) {
                NetUtil::sendError(server_, 409, "capture already active");
                return;
            }
            events.push("capture.start", "ram_local");
        } else if (strcmp(mode, "ram_wifi_off") == 0) {
            uint16_t ms = body["wifiOffMs"] | 5000;
            if (ms < 50 || ms > 60000) {
                NetUtil::sendError(server_, 400, "wifiOffMs 50..60000");
                return;
            }
            if (!mess.beginCapture(MessCaptureMode::RamWifiOff)) {
                NetUtil::sendError(server_, 409, "capture already active");
                return;
            }
            beginCaptureWifiOff(ms);
        } else if (mode[0]) {
            NetUtil::sendError(server_, 400, "mode ram_local|ram_wifi_off");
            return;
        }
        if (body["stop"] | false) {
            if (!mess.captureActive()) {
                NetUtil::sendError(server_, 409, "no active capture");
                return;
            }
            if (captureWifiOff_ || captureWifiOffAtMs_) {
                NetUtil::sendError(server_, 409, "ram_wifi_off ends on timer");
                return;
            }
            mess.endCapture(body["reason"] | "api_stop");
            events.push("capture.stop", mess.captureModeName());
        }
        if (body["mark"].is<const char*>()) mess.mark(body["mark"].as<const char*>());
    }
    JsonDocument doc;
    doc["ok"] = true;
    JsonObject meta = doc["mess"].to<JsonObject>();
    mess.toJson(meta);
    meta["wifiOff"] = captureWifiOff_;
    meta["wifiOffPending"] = captureWifiOffAtMs_ != 0;
#if defined(PIDRIVE_MESS) && PIDRIVE_MESS
    doc["cmdCapture"] = true;
#else
    doc["cmdCapture"] = false;
    doc["hint"] = "flash env pidrive-s3-mess for READ10 cmd capture";
#endif
    if (!mess.captureActive()) {
        MessCmd buf[64];
        uint64_t after =
            server_.hasArg("after") ? strtoull(server_.arg("after").c_str(), nullptr, 10) : 0;
        size_t n = after ? mess.copyAfter(after, buf, 64) : mess.copyRecent(buf, 64);
        JsonArray arr = doc["cmds"].to<JsonArray>();
        for (size_t i = 0; i < n; i++) {
            JsonObject o = arr.add<JsonObject>();
            o["seq"] = buf[i].seq;
            o["t0_us"] = buf[i].t0Us;
            o["dur_us"] =
                (buf[i].t1Us >= buf[i].t0Us) ? (buf[i].t1Us - buf[i].t0Us) : 0;
            o["gap_us"] = buf[i].gapUs;
            o["op"] = buf[i].op;
            o["lba"] = buf[i].lba;
            o["bytes"] = buf[i].bytes;
            o["blocks"] = buf[i].bytes / 512u;
            o["chunks"] = buf[i].chunks;
            o["busy"] = buf[i].busyN;
            o["st"] = buf[i].status;
            o["source"] = buf[i].source == MessSource::Synthesized ? "synthesized" : "marker";
            o["synthesized"] = buf[i].synthesized;
            if (buf[i].tag[0]) o["tag"] = buf[i].tag;
        }
    } else {
        doc["telemetrySuppressed"] = true;
    }
    NetUtil::sendJson(server_, 200, doc);
}

/** POST {"name":"usb_cycle"|"media_remount"|"stop"|"stall_on"|"stall_off"|"identity"|"mark",
 *         "holdMs":600,"keepIdentity":false,"mark":"...","maxMs":500} */
void App::handleApiLabSituation() {
    if (!requireLabMode()) return;
    if (captureWifiOff_ || captureWifiOffAtMs_) {
        NetUtil::sendError(server_, 409, "capture control owns offline window");
        return;
    }
    JsonDocument body;
    if (!NetUtil::readJsonBody(server_, body)) return;
    const char* name = body["name"] | "";
    if (!name[0]) {
        NetUtil::sendError(server_, 400, "name required");
        return;
    }
    auto& mess = MessCapture::instance();
    bool ok = true;
    String detail = name;

    if (strcmp(name, "usb_cycle") == 0) {
        uint16_t holdMs = (uint16_t)(body["holdMs"] | 600);
        if (holdMs < 50) holdMs = 50;
        if (holdMs > 5000) holdMs = 5000;
        if (stream.active()) {
            stream.stop();
            msc.stopStream();
            menu.clearPlaying();
        }
        msc.armSoftUsbKeepIdentity();
        ok = mess.usbDetach(holdMs);
        mess.mark("usb_cycle");
    } else if (strcmp(name, "usb_detach") == 0) {
        if (stream.active()) {
            stream.stop();
            msc.stopStream();
            menu.clearPlaying();
        }
        msc.armSoftUsbKeepIdentity();
        ok = mess.usbDetach(0);
        mess.mark("usb_detach");
    } else if (strcmp(name, "usb_attach") == 0) {
        ok = mess.usbAttach();
        mess.mark("usb_attach");
    } else if (strcmp(name, "media_remount") == 0) {
        if (stream.active()) {
            stream.stop();
            msc.stopStream();
            menu.clearPlaying();
        }
        bool keepId = body["keepIdentity"] | false;
        if (keepId) msc.remountMediaKeepIdentity("sit");
        else msc.remountMedia("sit");
        mess.mark("media_remount");
    } else if (strcmp(name, "stop") == 0) {
        stream.stop();
        msc.stopStream();
        menu.clearPlaying();
        mess.mark("stop");
    } else if (strcmp(name, "stall_on") == 0) {
        config.stallEnable = true;
        if (!body["maxMs"].isNull()) config.stallMaxMs = (uint16_t)(body["maxMs"] | 500);
        if (!body["scope"].isNull()) config.stallScope = (uint8_t)(body["scope"] | 1);
        config.save();
        applyStallFromConfig();
        mess.mark("stall_on");
    } else if (strcmp(name, "stall_off") == 0) {
        config.stallEnable = false;
        config.save();
        applyStallFromConfig();
        mess.mark("stall_off");
    } else if (strcmp(name, "identity") == 0) {
        if (stream.active()) {
            stream.stop();
            msc.stopStream();
            menu.clearPlaying();
        }
        msc.remountMedia("identity");
        mess.mark("identity");
    } else if (strcmp(name, "mark") == 0) {
        const char* tag = body["mark"] | "mark";
        mess.mark(tag);
        detail = tag;
    } else if (strcmp(name, "reset_capture") == 0) {
        mess.reset();
        mess.mark("reset");
    } else {
        NetUtil::sendError(server_, 400,
                           "name: usb_cycle|usb_detach|usb_attach|media_remount|stop|"
                           "stall_on|stall_off|identity|mark|reset_capture");
        return;
    }

    events.push("lab.situation", detail.c_str());
    JsonDocument doc;
    doc["ok"] = ok;
    doc["name"] = name;
    doc["usbAttached"] = mess.usbAttached();
    doc["otgPlugged"] = msc.plugged();
    doc["mediaPresented"] = msc.mediaPresented();
    doc["streamActive"] = stream.active();
    doc["note"] = "soft_usb≠VBUS; media_remount≠usb_cycle";
    NetUtil::sendJson(server_, ok ? 200 : 500, doc);
}

void App::handleApiLabListen() {
    if (!stream.active() || stream.size() < 2048) {
        server_.send(503, F("text/plain"), F("kein Live-Stream — Station per Menü/Play starten"));
        return;
    }
    events.push("audio.listen", stream.uid());

    // Raw body (kein chunked) — sonst kaputt mit client.write(Binary)
    WiFiClient client = server_.client();
    client.print(F("HTTP/1.1 200 OK\r\n"));
    client.print(F("Content-Type: audio/mpeg\r\n"));
    client.print(F("Cache-Control: no-store\r\n"));
    client.print(F("Connection: close\r\n"));
    client.print(F("X-Stream-Uid: "));
    client.print(stream.uid());
    client.print(F("\r\n\r\n"));

    uint32_t pos = stream.absBase();
    if (stream.size() > 12288) {
        pos = stream.absEnd() - 12288;
        if (pos < stream.absBase()) pos = stream.absBase();
    }
    uint8_t buf[1024];
    uint32_t lastByteMs = millis();
    while (client.connected() && stream.active()) {
        pump.loop();
        uart.loop();
        if (pos < stream.absBase()) pos = stream.absBase();
        size_t got = stream.copyFrom(pos, buf, sizeof(buf));
        if (got) {
            size_t w = client.write(buf, got);
            if (w == 0) break;
            pos += (uint32_t)w;
            lastByteMs = millis();
        } else {
            delay(15);
            if (millis() - lastByteMs > 15000) break;
        }
        yield();
    }
    client.stop();
}

void App::handleApiMetrics() {
    JsonDocument doc;
    doc["ok"] = true;
    doc["uptimeMs"] = millis() - bootMs_;
    doc["freeHeap"] = ESP.getFreeHeap();
    JsonObject m = doc["msc"].to<JsonObject>();
    msc.toJson(m);
    JsonObject mess = doc["mess"].to<JsonObject>();
    MessCapture::instance().toJson(mess);
    JsonArray tr = doc["mscTrace"].to<JsonArray>();
    msc.traceToJson(tr);
    JsonObject u = doc["uart"].to<JsonObject>();
    uart.toJson(u);
    doc["otgUp"] = msc.plugged();
    doc["uartUp"] = uart.linkUp();
    doc["playingUid"] = menu.playingUid();
    doc["playingName"] = menu.playingName();
    doc["eventCount"] = events.count();
    doc["liveAvailBytes"] = stream.liveAvailBytes();
    doc["liveAvailMs"] = stream.liveAvailMs();
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

void App::applyPlayDetectFromConfig() {
    PlayDetectParams p;
    p.plugWindowMs = config.playPlugWindowMs;
    p.minSeqBytes = config.playMinSeqBytes;
    p.navMinSeqBytes = config.playNavMinSeqBytes;
    p.headLbaSlop = config.playHeadLbaSlop;
    p.cooldownMs = config.playCooldownMs;
    p.prefetchLbaSlop = config.playPrefetchLbaSlop;
    msc.setPlayDetectParams(p);
    Serial.printf("[MSC] playDetect plug=%ums seq=%u nav=%u head=%u cd=%ums pf=%u\n",
                  (unsigned)p.plugWindowMs, (unsigned)p.minSeqBytes, (unsigned)p.navMinSeqBytes,
                  (unsigned)p.headLbaSlop, (unsigned)p.cooldownMs, (unsigned)p.prefetchLbaSlop);
}

void App::applyStallFromConfig() {
    StallParams s;
    s.enable = config.stallEnable;
    s.scope = config.stallScope;
    s.timer = config.stallTimer;
    s.maxMs = config.stallMaxMs;
    s.chunkBytes = config.stallChunkBytes;
    s.retryMs = config.stallRetryMs;
    s.leadBytes = config.leadBytes;
    s.underrunCursorHold = config.underrunCursorHold;
    s.silenceMatchLive = config.silenceMatchLive;
    msc.setStallParams(s);
    SelectionBarrierParams b;
    b.enable = config.selectionBarrierEnable;
    b.maxMs = config.selectionBarrierMaxMs;
    b.audioLeadBytes = config.selectionBarrierLeadBytes;
    msc.setSelectionBarrierParams(b);
    msc.setVolumeLabelOverride(config.volumeLabel.c_str());
    msc.setFatVolumeIdOverride(config.fatVolumeId);
    Serial.printf("[MSC] stall en=%d scope=%u timer=%u max=%ums chunk=%u retry=%ums vol='%s' id=%08lX\n",
                  (int)s.enable, (unsigned)s.scope, (unsigned)s.timer, (unsigned)s.maxMs,
                  (unsigned)s.chunkBytes, (unsigned)s.retryMs, config.volumeLabel.c_str(),
                  (unsigned long)config.fatVolumeId);
}

void App::beginCaptureWifiOff(uint16_t durationMs) {
    // Delay shutdown until the HTTP response has left. App is the sole transition owner;
    // the TinyUSB callback only appends POD records to MessCapture.
    captureWifiOffAtMs_ = millis() + 100;
    captureWifiOnAtMs_ = captureWifiOffAtMs_ + durationMs;
    events.push("capture.offline.arm", String(durationMs).c_str());
}

void App::serviceCaptureWifiOff() {
    const uint32_t now = millis();
    if (captureWifiOffAtMs_ && (int32_t)(now - captureWifiOffAtMs_) >= 0) {
        captureWifiOffAtMs_ = 0;
        WiFi.mode(WIFI_OFF);
        captureWifiOff_ = true;
    }
    if (captureWifiOff_ && captureWifiOnAtMs_ &&
        (int32_t)(now - captureWifiOnAtMs_) >= 0) {
        captureWifiOnAtMs_ = 0;
        captureWifiOff_ = false;
        if (config.enableSoftAp && config.enableSta) WiFi.mode(WIFI_AP_STA);
        else if (config.enableSoftAp) WiFi.mode(WIFI_AP);
        else WiFi.mode(WIFI_STA);
        if (config.enableSoftAp) startSoftAp();
        if (config.enableSta) WiFi.begin();  // reconnect using stored credentials, non-blocking
        if (config.enablePumpTcp) pump.startTcp(config.pumpTcpPort);
        MessCapture::instance().endCapture("wifi_window_complete");
        events.push("capture.offline.done", "");
    }
}
