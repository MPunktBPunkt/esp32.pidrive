#include "StatusLed.h"

#if defined(RGB_BUILTIN)
#include "esp32-hal-rgb-led.h"
#include "soc/soc_caps.h"
#endif

void StatusLed::begin() {
#if defined(RGB_BUILTIN)
    pin_ = (uint8_t)(RGB_BUILTIN - SOC_GPIO_PIN_COUNT);
    ok_ = true;
#else
    pin_ = 48;
    ok_ = true;
#endif
    bootUntilMs_ = millis() + 1200;
    mode_ = Mode::Boot;
    writeRgb(8, 8, 20);  // kurzes Blau beim Boot
    shown_ = Mode::Boot;
    Serial.printf("[LED] RGB pin=%u\n", pin_);
}

void StatusLed::writeRgb(uint8_t r, uint8_t g, uint8_t b) {
    if (!ok_) return;
#if defined(RGB_BUILTIN)
    neopixelWrite(RGB_BUILTIN, r, g, b);
#else
    neopixelWrite(pin_, r, g, b);
#endif
}

void StatusLed::setMode(Mode m) {
    if (millis() < bootUntilMs_ && m != Mode::Error) return;
    mode_ = m;
}

const char* StatusLed::modeName() const {
    switch (mode_) {
        case Mode::Boot: return "boot";
        case Mode::Idle: return "idle";
        case Mode::OtgMount: return "otg";
        case Mode::UartLink: return "uart";
        case Mode::Error: return "error";
        case Mode::Off: return "off";
    }
    return "?";
}

void StatusLed::applySolid() {
    switch (mode_) {
        case Mode::Boot:
            writeRgb(8, 8, 24);
            break;
        case Mode::Idle:
            writeRgb(0, 28, 0);  // Grün — FW läuft
            break;
        case Mode::OtgMount:
            writeRgb(36, 22, 0);  // Gelb — OTG Host mount
            break;
        case Mode::UartLink:
            writeRgb(0, 18, 28);  // Cyan — UART aktiv
            break;
        case Mode::Error:
            writeRgb(40, 0, 0);
            break;
        case Mode::Off:
            writeRgb(0, 0, 0);
            break;
    }
    shown_ = mode_;
}

void StatusLed::loop() {
    if (!ok_) return;
    if (millis() >= bootUntilMs_ && mode_ == Mode::Boot) {
        mode_ = Mode::Idle;
    }
    if (mode_ != shown_) applySolid();
}
