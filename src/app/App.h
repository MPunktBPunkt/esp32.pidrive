#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include "core/ConfigStore.h"
#include "core/HubClient.h"
#include "core/EventLog.h"
#include "core/MenuStore.h"
#include "core/UartLinkMonitor.h"
#include "core/StatusLed.h"
#include "core/PumpServer.h"
#include "msc/UsbMscGadget.h"

class App {
public:
    static App& instance();
    void begin();
    void loop();

    ConfigStore config;
    HubClient hub;
    EventLog events;
    MenuStore menu;
    UsbMscGadget msc;
    UartLinkMonitor uart;
    StatusLed led;
    PumpServer pump;

    bool pumpUp = false;
    uint16_t bufferMs = 0;
    String softApSsid;
    String softApIp = "192.168.4.1";

private:
    App() = default;
    void setupWifi();
    void startSoftAp();
    void setupWeb();
    void buildHeartbeat(JsonDocument& doc);
    void buildStatus(JsonDocument& doc);
    void handleRoot();
    void handleApiStatus();
    void handleApiMenu();
    void handleApiEvents();
    void handleApiEventsClear();
    void handleApiConfigGet();
    void handleApiConfigPost();
    void handleApiLabPlay();
    void handleApiMetrics();
    void handleOtaUpload();
    void handleRestart();

    WebServer server_{80};
    unsigned long bootMs_ = 0;
};
