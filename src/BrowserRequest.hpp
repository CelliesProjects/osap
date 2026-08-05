#pragma once

#include <Arduino.h>
#include <PsychicWebSocket.h>

struct ListRequest
{
    PsychicWebSocketClient *client;
    char path[256];
};
