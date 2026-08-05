#pragma once

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "SearchRequest.hpp"

extern PsychicWebSocketHandler websocketHandler;
extern void msgToClient(const char *msg, PsychicWebSocketClient *c);
extern QueueHandle_t searchQueue;

void searchTask(void *param);