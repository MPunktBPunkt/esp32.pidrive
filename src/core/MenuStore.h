#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "BuildFlags.h"

struct MenuItem {
    char path[48];
    char name[40];
    char uid[24];   // decimal uint64 from Pi, or demo:*
    char kind[12];  // station | action | folder | info
    bool playing;
};

class MenuStore {
public:
    void begin();
    void loadDemo();
    /** Load last menu from NVS; false if empty/invalid. */
    bool loadFromNvs();
    void saveToNvs() const;
    bool fromNvs() const { return fromNvs_; }

    /**
     * Replace items from PUMP menu_set.
     * @return true if uid/name/kind content changed (rev-only updates return false).
     */
    bool setFromJson(JsonArrayConst items, uint32_t rev = 0);
    void setPlaying(const char* uid);
    void clearPlaying();
    const char* playingUid() const { return playingUid_; }
    const char* playingName() const;
    size_t count() const { return count_; }
    uint32_t rev() const { return rev_; }
    const MenuItem* items() const { return items_; }
    const MenuItem* itemAt(size_t i) const { return i < count_ ? &items_[i] : nullptr; }
    void toJson(JsonObject obj) const;
    bool playByUid(const char* uid);

private:
    MenuItem items_[MENU_MAX_ITEMS];
    size_t count_ = 0;
    char playingUid_[24] = {0};
    uint32_t rev_ = 0;
    bool fromNvs_ = false;
};
