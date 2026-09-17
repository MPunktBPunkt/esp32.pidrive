#include "PumpServer.h"
#include "BuildFlags.h"
#include <ArduinoJson.h>
#include <cstring>

void PumpServer::begin(EventLog* events, MenuStore* menu, UsbMscGadget* msc, UartLinkMonitor* uart) {
    events_ = events;
    menu_ = menu;
    msc_ = msc;
    uart_ = uart;
    lineLen_ = 0;
    up_ = false;
    if (events_) events_->push("pump.init", "line-json");
}

void PumpServer::sendRaw(const char* s) {
    if (!s) return;
    Serial.println(s);
}

void PumpServer::sendJson(const JsonDocument& doc) {
    String out;
    serializeJson(doc, out);
    sendRaw(out.c_str());
}

void PumpServer::sendPlayUid(const char* uid) {
    if (!uid || !uid[0]) return;
    JsonDocument doc;
    doc["t"] = "event";
    doc["op"] = "play_uid";
    doc["uid"] = uid;
    sendJson(doc);
    if (events_) events_->push("pump.event", uid);
}

void PumpServer::handleLine(char* line) {
    while (*line == ' ' || *line == '\t') line++;
    if (!line[0]) return;

    // Lab plaintext
    if (!strncmp(line, "PING", 4) || !strncmp(line, "DBG", 3)) {
        Serial.println("PONG");
        return;
    }

    if (line[0] != '{') return;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, line);
    if (err) {
        if (events_) events_->push("pump.badjson", err.c_str());
        return;
    }

    const char* t = doc["t"] | "";
    if (!strcmp(t, "hello")) {
        up_ = true;
        JsonDocument ack;
        ack["t"] = "hello_ack";
        ack["ver"] = FW_VERSION;
        ack["fw"] = FW_TYPE;
        ack["slots"] = 4;
        sendJson(ack);
        if (events_) events_->push("pump.hello", "ok");
        return;
    }

    if (!strcmp(t, "menu_set")) {
        JsonArrayConst items = doc["items"].as<JsonArrayConst>();
        uint32_t rev = doc["rev"] | 0;
        if (!menu_ || items.isNull()) {
            JsonDocument nak;
            nak["t"] = "menu_ack";
            nak["ok"] = false;
            sendJson(nak);
            return;
        }
        menu_->setFromJson(items, rev);
        if (msc_) msc_->applyMenuSlots(*menu_);
        up_ = true;
        JsonDocument ack;
        ack["t"] = "menu_ack";
        ack["ok"] = true;
        ack["n"] = (int)menu_->count();
        ack["rev"] = rev;
        sendJson(ack);
        char det[40];
        snprintf(det, sizeof(det), "n=%u rev=%lu", (unsigned)menu_->count(), (unsigned long)rev);
        if (events_) events_->push("menu.set", det);
        return;
    }

    if (!strcmp(t, "ping")) {
        JsonDocument pong;
        pong["t"] = "pong";
        sendJson(pong);
    }
}

void PumpServer::loop() {
    while (Serial.available() > 0) {
        int c = Serial.read();
        if (c < 0) break;
        if (uart_) uart_->noteRx(1);
        if (c == '\n' || c == '\r') {
            if (lineLen_ > 0) {
                line_[lineLen_] = 0;
                handleLine(line_);
                lineLen_ = 0;
            }
            continue;
        }
        if (lineLen_ + 1 < sizeof(line_)) {
            line_[lineLen_++] = (char)c;
        } else {
            lineLen_ = 0;
        }
    }
}
