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

void UartLinkMonitor::noteRx(size_t n) {
    if (n == 0) return;
    rxBytes_ += (uint32_t)n;
    lastRxMs_ = millis();
    setState(State::Up, "rx");
}

void UartLinkMonitor::loop() {
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
