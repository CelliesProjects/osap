#pragma once

#include <SD.h>

#include <VS1053.h>              /* https://github.com/baldram/ESP_VS1053_Library */
#include <ESP32_VS1053_Stream.h> /* https://github.com/CelliesProjects/ESP32_VS1053_Stream */
#include <PsychicWebSocket.h>    /* https://github.com/hoeken/PsychicHttp */

#include "SystemState.hpp"
#include "Playlist.hpp"
#include "ScopedMutex.hpp"
#include "PlayerCmd.hpp"
#include "presets.hpp"
#include "normalizeToUtf8.hpp"
#include "gpio.hpp"

extern SemaphoreHandle_t sdMutex;
extern PsychicWebSocketHandler websocketHandler;
extern void msgToClient(const char *msg, PsychicWebSocketClient *c);
extern void oledMessage(SystemState state, const char *msg);
extern QueueHandle_t playerQueue;

extern const char *ERROR_PLAYER_BUSY;
extern void broadcastPlayerBusy();

extern SystemState systemState;

const char *FAVORITES_DIR = "/.favorites/";
void playerTask(void *param);
