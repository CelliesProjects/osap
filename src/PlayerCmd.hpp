#pragma once

#include <Arduino.h>
#include <PsychicWebSocket.h>

enum class PlayerCmdType
{
    PLAY_INDEX,
    REMOVE_INDEX,

    SET_VOLUME,
    SEND_VOLUME,

    PREVIOUS,
    NEXT,
    JUMPTO, // triggered from progressbar on web ui

    SEND_PLAYLIST,
    SEND_PRESETS,
    SEND_FAVORITES,
    SEND_STREAMTITLE,
    SEND_CODEC,
    SEND_BITRATE,

    ADD_PRESET,
    ADD_PATH,

    ADD_SEARCH,
    PLAY_SEARCH,

    ADD_FAVORITE,
    PLAY_FAVORITE,
    SAVE_FAVORITE,
    DELETE_FAVORITE,

    CLEAR_PLAYLIST,
    EOF_REACHED,
};

struct PlayerCmd
{
    PlayerCmdType type;
    PsychicWebSocketClient *client = nullptr;
    int index = -1;
    size_t position = 0;
    char path[256] = {0};
    char name[256] = {0};
    bool startNow = false;
    int volume = -1;
};
