#include "browserTask.hpp"

static char chunkHeader[256];
static ListRequest req;
static String chunk;
static FolderCacheItem cache[MAX_CACHE_ITEMS];

void browserTask(void *param)
{
    constexpr const char *LIST_FOOTER = "LIST:DONE:";

    chunk.reserve(2048);

    while (1)
    {
        if (xQueueReceive(browserQueue, &req, portMAX_DELAY) != pdTRUE)
            continue;

        log_d("listing path: %s", req.path);

        // check if the requested path is cached and if so, send the cached version
        // then send LIST:DONE: as a separate msg
        // continue;

        unsigned long startMS = millis();

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

        static String cached;

        cached = "";

        snprintf(chunkHeader, sizeof(chunkHeader), "LIST:%s\n", req.path);

        int count = 0;
        chunk = chunkHeader;

        while (true)
        {
            auto client = websocketHandler.getClient(req.client);
            if (!client)
            {
                log_w("client gone, abort listing");
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
                cached += chunk;
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
            cached += chunk;
            msgToClient(chunk.c_str(), req.client);
        }

        dir.close();

        msgToClient(LIST_FOOTER, req.client);

        const unsigned long duration = millis() - startMS;

        log_d("'%s' cached size: %d", req.path, cached.length());
        log_d("cached: %s", cached.c_str());
        log_i("duration: %lums", duration);

        if (duration > CACHE_THRESHOLD_MS)
        {
            // put req.path + cached into cache
            log_i("'%s' would be cached", req.path);
        }
    }
}
