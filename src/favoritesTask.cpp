#include "favoritesTask.hpp"

static FavoritesRequest req;
static String msg;

static void sendFavorites(PsychicWebSocketClient *c = nullptr)
{
    msg.clear();
    msg += "FAVORITES:\n";

    File dir;

    {
        ScopedMutex lock(sdMutex);
        dir = SD.open(FAVORITES_DIR);
    }

    if (!dir || !dir.isDirectory())
    {
        if (c)
            msgToClient(msg.c_str(), c);
        else
            websocketHandler.sendAll(msg.c_str());

        return;
    }

    while (true)
    {
        File file;

        {
            ScopedMutex lock(sdMutex);
            file = dir.openNextFile();
        }

        if (!file)
            break;

        if (file.isDirectory())
            continue;

        String name;

        while (true)
        {
            String line;

            {
                ScopedMutex lock(sdMutex);

                if (!file.available())
                    break;

                line = file.readStringUntil('\n');
            }

            line.trim();

            if (line.startsWith("NAME:"))
            {
                name = line.substring(5);
                break;
            }
        }

        {
            ScopedMutex lock(sdMutex);
            file.close();
        }

        if (name.length())
        {
            msg += name;
            msg += "\n";
        }
    }

    if (c)
        msgToClient(msg.c_str(), c);
    else
        websocketHandler.sendAll(msg.c_str());
}

void favoritesTask(void *param)
{
    log_d("favoritesTask running");

    msg.reserve(4096);

    while (1)
    {
        log_v("stack high water mark: %i", uxTaskGetStackHighWaterMark(NULL));

        if (xQueueReceive(favoritesQueue, &req, portMAX_DELAY) != pdTRUE)
        {
            log_e("could not queue favorites request");
            continue;
        }

        sendFavorites(req.client);
    }
}
