#pragma once

#include <PsychicWebSocket.h>

struct SearchRequest
{
    PsychicWebSocketClient *client;
    int page;
    char query[128];
};