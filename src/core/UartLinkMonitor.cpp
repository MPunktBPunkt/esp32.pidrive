#include "UartLinkMonitor.h"

void UartLinkMonitor::begin(EventLog* events) {
    events_ = events;
    changeMs_ = millis();
    if (events_) events_->push("usb.uart.init", "activity-sense");
}

void UartLinkMonitor::setLink(bool up, const char* detail) {
    if (linkUp_ == up) return;
    linkUp_ = up;
    changeMs_ = millis();
    changeCount_++;
    if (events_) events_->push(up ? "usb.uart.up" : "usb.uart.down", detail);
    Serial.printf("[UART] %s (%s)\n", up ? "UP" : "DOWN", detail);
}

void UartLinkMonitor::loop() {
    int n = Serial.available();
    if (n > 0) {
        // Drain so buffer doesn't stick "up" forever after host gone.
        // PUMP will own the serial later; until then discard is fine.
        uint8_t buf[64];
        while (n > 0) {
            int chunk = n > (int)sizeof(buf) ? (int)sizeof(buf) : n;
            int got = Serial.readBytes(buf, chunk);
            if (got <= 0) break;
            rxBytes_ += (uint32_t)got;
            n -= got;
        }
        lastRxMs_ = millis();
        setLink(true, "rx");
        return;
    }
    if (linkUp_ && lastRxMs_ && (millis() - lastRxMs_) > kIdleMs) {
        setLink(false, "idle");
    }
}

uint32_t UartLinkMonitor::msSinceChange() const {
    if (!changeMs_) return 0;
    return millis() - changeMs_;
}

void UartLinkMonitor::toJson(JsonObject obj) const {
    obj["linkUp"] = linkUp_;
    obj["rxBytes"] = rxBytes_;
    obj["lastRxMs"] = lastRxMs_;
    obj["msSinceChange"] = msSinceChange();
    obj["changeCount"] = changeCount_;
    obj["sense"] = "serial-activity";
    obj["idleMs"] = kIdleMs;
}
