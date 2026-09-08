#pragma once

#include <Arduino.h>

struct FolderCacheItem
{
    String path;
    String response;
    time_t timestamp;
};
