#pragma once

#include <Arduino.h>
#include <FS.h>
#include <SD.h>

#include <PsychicWebSocket.h>

#include "BrowserRequest.hpp"
#include "ScopedMutex.hpp"

extern SemaphoreHandle_t sdMutex;
extern QueueHandle_t browserQueue;
extern PsychicWebSocketHandler websocketHandler;
extern void msgToClient(const char *msg, PsychicWebSocketClient *c);

void browserTask(void *param);
