#include "MenuStore.h"
#include <cstring>

void MenuStore::begin() {
    count_ = 0;
    playingUid_[0] = 0;
    loadDemo();
}

void MenuStore::loadDemo() {
    count_ = 0;
    auto add = [&](const char* path, const char* name, const char* uid, const char* kind) {
        if (count_ >= MENU_MAX_ITEMS) return;
        MenuItem& it = items_[count_++];
        memset(&it, 0, sizeof(it));
        strncpy(it.path, path, sizeof(it.path) - 1);
        strncpy(it.name, name, sizeof(it.name) - 1);
        strncpy(it.uid, uid, sizeof(it.uid) - 1);
        strncpy(it.kind, kind, sizeof(it.kind) - 1);
        it.playing = false;
    };
    // Paths match USB FAT demo image (8.3 names)
    add("STATIONS/01ROCK.MP3", "ROCK FM", "demo:rock_fm", "station");
    add("STATIONS/02ANTENN.MP3", "Antenne 1", "demo:antenne", "station");
    add("STATIONS/03SWR3.MP3", "SWR3", "demo:swr3", "station");
    add("SETTINGS/ABOUT.MP3", "About", "action:about", "action");
}

void MenuStore::setPlaying(const char* uid) {
    playingUid_[0] = 0;
    if (uid) {
        strncpy(playingUid_, uid, sizeof(playingUid_) - 1);
        playingUid_[sizeof(playingUid_) - 1] = 0;
    }
    for (size_t i = 0; i < count_; i++) {
        items_[i].playing = (playingUid_[0] && strcmp(items_[i].uid, playingUid_) == 0);
    }
}

void MenuStore::clearPlaying() {
    setPlaying("");
}

const char* MenuStore::playingName() const {
    for (size_t i = 0; i < count_; i++) {
        if (items_[i].playing) return items_[i].name;
    }
    return "";
}

bool MenuStore::playByUid(const char* uid) {
    if (!uid || !uid[0]) return false;
    for (size_t i = 0; i < count_; i++) {
        if (strcmp(items_[i].uid, uid) == 0) {
            setPlaying(uid);
            return true;
        }
    }
    return false;
}

void MenuStore::toJson(JsonObject obj) const {
    obj["count"] = count_;
    obj["playingUid"] = playingUid_;
    obj["playingName"] = playingName();
    JsonArray arr = obj["items"].to<JsonArray>();
    for (size_t i = 0; i < count_; i++) {
        JsonObject o = arr.add<JsonObject>();
        o["path"] = items_[i].path;
        o["name"] = items_[i].name;
        o["uid"] = items_[i].uid;
        o["kind"] = items_[i].kind;
        o["playing"] = items_[i].playing;
    }
}
