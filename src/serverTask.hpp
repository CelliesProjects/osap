#pragma once

#include <Arduino.h>
#include <SD.h>

#include <PsychicHttp.h>      /* https://github.com/hoeken/PsychicHttp */
#include <PsychicWebSocket.h> /* https://github.com/hoeken/PsychicHttp */

#include "ScopedMutex.hpp"
#include "BrowserRequest.hpp"
#include "presets.hpp"
#include "PlayerCmd.hpp"
#include "SearchRequest.hpp"
#include "FavoritesRequest.hpp"
#include "generated/build_info.hpp"

extern void runWiFiMulti();

extern QueueHandle_t browserQueue;
extern QueueHandle_t playerQueue;
extern QueueHandle_t searchQueue;
extern QueueHandle_t favoritesQueue;

extern SemaphoreHandle_t sdMutex;

extern String favoritesToCStruct();

const char *ERROR_FAVORITES_BUSY = "ERROR:Favorites task busy";
const char *ERROR_PLAYER_BUSY = "ERROR:Player busy";

void serverTask(void *param);

PsychicWebSocketHandler websocketHandler;
