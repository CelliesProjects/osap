#include "browserTask.hpp"

static char chunkHeader[256];
static ListRequest req;
static String chunk;
static FolderCacheItem cache[MAX_CACHE_ITEMS];

static FolderCacheItem *findCached()
{
    for (auto &item : cache)
    {
        if (item.path == req.path)
        {
            item.timestamp = time(nullptr);
            return &item;
        }
    }

    return nullptr;
}

static void cacheRequest()
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

    // Cache is full: evict the least recently used entry
    if (index == -1)
    {
        index = 0;

        for (int i = 1; i < MAX_CACHE_ITEMS; ++i)
        {
            if (cache[i].timestamp < cache[index].timestamp)
                index = i;
        }

        log_i("evicting '%s' from cache", cache[index].path.c_str());
    }

    cache[index].path = req.path;
    cache[index].response = std::move(cacheBuffer);
    cache[index].timestamp = time(nullptr);
}

static void processItems(File &dir)
{
    snprintf(chunkHeader, sizeof(chunkHeader), "LIST:%s\n", req.path);

    chunk = chunkHeader;
    cacheBuffer = "";

    int count = 0;

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

    msgToClient(LIST_DONE, req.client);
}

static void serveFromCache(FolderCacheItem *item)
{
    msgToClient(item->response.c_str(), req.client);
    vTaskDelay(1);
    msgToClient(LIST_DONE, req.client);
}

static bool openFolder(File &dir)
{
    {
        ScopedMutex lock(sdMutex);
        dir = SD.open(req.path);
    }

    if (!dir || !dir.isDirectory())
    {
        if (strlen(req.path) == 1)
        {
            msgToClient("ERROR:SD card not mounted", req.client);
            return false;
        }

        msgToClient("ERROR:not a directory", req.client);
        return false;
    }
    return true;
}

void browserTask(void *param)
{
    chunk.reserve(2048);

    while (true)
    {
        if (xQueueReceive(browserQueue, &req, portMAX_DELAY) != pdTRUE)
            continue;

        const auto startMS = millis();

        if (auto *item = findCached())
        {
            serveFromCache(item);
            log_i("cache hit: '%s' - %d ms", req.path, millis() - startMS);
            continue;
        }

        File dir;

        if (!openFolder(dir))
            continue;

        processItems(dir);

        dir.close();

        const auto duration = millis() - startMS;
        const auto client = websocketHandler.getClient(req.client);

        if (duration < CACHE_THRESHOLD_MS || !client)
            continue;

        log_i("caching '%s', %d ms, %u bytes", req.path, duration, cacheBuffer.length());

        cacheRequest();
    }
}
