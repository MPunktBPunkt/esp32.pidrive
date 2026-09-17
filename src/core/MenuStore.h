#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "BuildFlags.h"

struct MenuItem {
    char path[48];
    char name[40];
    char uid[24];
    char kind[12];  // station | action
    bool playing;
};

class MenuStore {
public:
    void begin();
    void loadDemo();
    void setPlaying(const char* uid);
    void clearPlaying();
    const char* playingUid() const { return playingUid_; }
    const char* playingName() const;
    size_t count() const { return count_; }
    const MenuItem* items() const { return items_; }
    void toJson(JsonObject obj) const;
    bool playByUid(const char* uid);

private:
    MenuItem items_[MENU_MAX_ITEMS];
    size_t count_ = 0;
    char playingUid_[24] = {0};
};
