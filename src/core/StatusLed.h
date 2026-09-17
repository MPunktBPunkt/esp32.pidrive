#pragma once

#include <Arduino.h>

/** Onboard WS2812 (ESP32-S3 DevKitC-1: GPIO48 via RGB_BUILTIN). */
class StatusLed {
public:
    enum class Mode : uint8_t {
        Boot = 0,
        Idle,       // FW läuft — Grün
        OtgMount,   // Auto/PC Host gemountet — Gelb
        UartLink,   // UART-Aktivität, kein OTG — Cyan
        Error,      // Rot
        Off
    };

    void begin();
    void loop();
    void setMode(Mode m);
    Mode mode() const { return mode_; }
    const char* modeName() const;

private:
    void writeRgb(uint8_t r, uint8_t g, uint8_t b);
    void applySolid();

    Mode mode_ = Mode::Boot;
    Mode shown_ = Mode::Off;
    uint32_t bootUntilMs_ = 0;
    uint8_t pin_ = 48;
    bool ok_ = false;
};
