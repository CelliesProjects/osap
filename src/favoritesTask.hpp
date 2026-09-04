#pragma once

#include <SD.h>

#include "FavoritesRequest.hpp"
#include "ScopedMutex.hpp"

constexpr int WS_MSG_RESERVED = 4096;

extern void msgToClient(const char *msg, PsychicWebSocketClient *c);
extern PsychicWebSocketHandler websocketHandler;
extern const char *FAVORITES_DIR;
extern QueueHandle_t favoritesQueue;
extern SemaphoreHandle_t sdMutex;
