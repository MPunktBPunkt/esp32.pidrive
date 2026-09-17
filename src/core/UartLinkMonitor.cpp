#include "UartLinkMonitor.h"

void UartLinkMonitor::begin(EventLog* events) {
    events_ = events;
    changeMs_ = millis();
    state_ = State::Idle;
    if (events_) events_->push("usb.uart.init", "activity-sense");
}

const char* UartLinkMonitor::stateName() const {
    switch (state_) {
        case State::Up: return "up";
        case State::Quiet: return "quiet";
        case State::Idle:
        default: return "idle";
    }
}

void UartLinkMonitor::setState(State s, const char* detail) {
    if (state_ == s) return;
    State prev = state_;
    state_ = s;
    changeMs_ = millis();
    changeCount_++;
    if (events_) {
        if (s == State::Up) events_->push("usb.uart.up", detail);
        else if (s == State::Quiet) events_->push("usb.uart.quiet", detail);
        else if (prev == State::Up || prev == State::Quiet) events_->push("usb.uart.idle", detail);
    }
    Serial.printf("[UART] %s (%s)\n", stateName(), detail);
}

void UartLinkMonitor::loop() {
    int n = Serial.available();
    if (n > 0) {
        uint8_t buf[64];
        size_t total = 0;
        char line[80];
        size_t lp = 0;
        while (n > 0) {
            int chunk = n > (int)sizeof(buf) ? (int)sizeof(buf) : n;
            int got = Serial.readBytes(buf, chunk);
            if (got <= 0) break;
            rxBytes_ += (uint32_t)got;
            total += (size_t)got;
            for (int i = 0; i < got; i++) {
                char c = (char)buf[i];
                if (c == '\n' || c == '\r') {
                    if (lp > 0) {
                        line[lp] = 0;
                        // Lab handshake — Pi kann Bidirektionalität prüfen
                        if (strncmp(line, "PING", 4) == 0 || strncmp(line, "DBG", 3) == 0) {
                            Serial.println("PONG");
                        }
                        lp = 0;
                    }
                } else if (lp + 1 < sizeof(line)) {
                    line[lp++] = c;
                }
            }
            n -= got;
        }
        (void)total;
        lastRxMs_ = millis();
        setState(State::Up, "rx");
        return;
    }
    if (state_ == State::Up && lastRxMs_ && (millis() - lastRxMs_) > kIdleMs) {
        setState(State::Quiet, "no-rx");
    }
}

uint32_t UartLinkMonitor::msSinceChange() const {
    if (!changeMs_) return 0;
    return millis() - changeMs_;
}

uint32_t UartLinkMonitor::msSinceRx() const {
    if (!lastRxMs_) return 0;
    return millis() - lastRxMs_;
}

void UartLinkMonitor::toJson(JsonObject obj) const {
    obj["linkUp"] = linkUp();
    obj["state"] = stateName();
    obj["rxBytes"] = rxBytes_;
    obj["lastRxMs"] = lastRxMs_;
    obj["msSinceChange"] = msSinceChange();
    obj["msSinceRx"] = msSinceRx();
    obj["changeCount"] = changeCount_;
    obj["sense"] = "serial-activity";
    obj["idleMs"] = kIdleMs;
    obj["note"] = "kein Plug-Sensor — up=Traffic, quiet=Kabel ok/still, idle=nie Traffic";
}
