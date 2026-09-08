#pragma once

#include <Arduino.h>
#include <FS.h>
#include <SD.h>

#include <PsychicWebSocket.h>

#include "FolderCacheItem.hpp"
#include "BrowserRequest.hpp"
#include "ScopedMutex.hpp"

constexpr int MAX_ITEMS_IN_CHUNK = 4;
constexpr size_t MAX_CACHE_ITEMS = 100;

extern SemaphoreHandle_t sdMutex;
extern QueueHandle_t browserQueue;
extern PsychicWebSocketHandler websocketHandler;
extern void msgToClient(const char *msg, PsychicWebSocketClient *c);

void browserTask(void *param);
