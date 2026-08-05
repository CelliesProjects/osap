#include "browserTask.hpp"

constexpr int MAX_ITEMS = 5;

static char chunkHeader[256];
static ListRequest req;

void browserTask(void *param)
{
    String chunk;
    chunk.reserve(2048);

    while (1)
    {
        if (xQueueReceive(browserQueue, &req, portMAX_DELAY) != pdTRUE)
            continue;

        log_d("listing path: %s", req.path);

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

        unsigned long startMS = millis();
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
            if (count >= MAX_ITEMS)
            {
                msgToClient(chunk.c_str(), req.client);
                chunk = chunkHeader;
                count = 0;
            }

            entry.close();

            vPortYield();
        }

        // send remainder
        if (count > 0)
            msgToClient(chunk.c_str(), req.client);

        dir.close();

        msgToClient("LIST:DONE:", req.client);

        log_v("duration: %lums", millis() - startMS);
    }
}
