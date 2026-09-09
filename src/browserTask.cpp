#include "browserTask.hpp"

static char chunkHeader[256];
static ListRequest req;
static String chunk;
static FolderCacheItem cache[MAX_CACHE_ITEMS];

static FolderCacheItem *findCached(const char *path)
{
    for (auto &item : cache)
    {
        if (item.path == path)
            return &item;
    }

    return nullptr;
}

static void cacheRequest(String &response)
{
    int index = -1;

    // Prefer an existing entry or an unused slot.
    for (int i = 0; i < MAX_CACHE_ITEMS; ++i)
    {
        if (cache[i].path == req.path || cache[i].path.isEmpty())
        {
            index = i;
            break;
        }
    }

    // Cache is full: evict the smallest entry and if tied the oldest
    if (index == -1)
    {
        index = 0;

        for (int i = 1; i < MAX_CACHE_ITEMS; ++i)
        {
            if (cache[i].response.length() < cache[index].response.length() ||
                (cache[i].response.length() == cache[index].response.length() &&
                 cache[i].timestamp < cache[index].timestamp))
            {
                index = i;
            }
        }

        log_i("evicting '%s' from cache", cache[index].path.c_str());
    }

    cache[index].path = req.path;
    cache[index].response = std::move(response);
    cache[index].timestamp = time(nullptr);
}

void browserTask(void *param)
{
    constexpr const char *LIST_DONE = "LIST:DONE:";

    chunk.reserve(2048);

    while (1)
    {
        if (xQueueReceive(browserQueue, &req, portMAX_DELAY) != pdTRUE)
            continue;

        log_d("listing path: %s", req.path);

        const auto startMS = millis();

        if (auto *item = findCached(req.path))
        {
            msgToClient(item->response.c_str(), req.client);
            vTaskDelay(1);
            msgToClient(LIST_DONE, req.client);
            log_i("%d ms - '%s' served from cache", millis() - startMS, req.path);
            continue;
        }

        File dir;
        {
            ScopedMutex lock(sdMutex);
            dir = SD.open(req.path);
        }

        if (!dir || !dir.isDirectory())
        {
            if (strlen(req.path) == 1)
            {
                msgToClient("ERROR:SD card not mounted", req.client);
                continue;
            }

            msgToClient("ERROR:not a directory", req.client);
            continue;
        }

        snprintf(chunkHeader, sizeof(chunkHeader), "LIST:%s\n", req.path);

        int count = 0;
        chunk = chunkHeader;

        static String cacheBuffer;
        cacheBuffer = "";

        while (true)
        {
            auto client = websocketHandler.getClient(req.client);
            if (!client)
            {
                log_w("client gone, abort listing");
                count = 0;
                break;
            }

            File entry;
            {
                ScopedMutex lock(sdMutex);
                entry = dir.openNextFile();
            }

            if (!entry)
                break;

            if (entry.name()[0] == '.') // hide system folders
                continue;

            chunk += (entry.isDirectory() ? "D:" : "F:");
            chunk += entry.name();
            chunk += '\n';

            count++;

            // send chunk
            if (count >= MAX_ITEMS_IN_CHUNK)
            {
                cacheBuffer += chunk;
                msgToClient(chunk.c_str(), req.client);
                chunk = chunkHeader;
                count = 0;
                vPortYield();
            }

            entry.close();
        }

        // send remainder
        if (count > 0)
        {
            cacheBuffer += chunk;
            msgToClient(chunk.c_str(), req.client);
        }

        dir.close();

        msgToClient(LIST_DONE, req.client);

        const auto duration = millis() - startMS;
        const auto client = websocketHandler.getClient(req.client);

        if (duration < CACHE_THRESHOLD_MS || !client)
            continue;

        log_i("%d ms - '%s' qualifies for caching - size: %u bytes", duration, req.path, cacheBuffer.length());

        cacheRequest(cacheBuffer);
    }
}
