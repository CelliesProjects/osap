#include "favoritesTask.hpp"

static FavoritesRequest req;
static String webSocketMsg;

static void handleFolder(File &dir)
{
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
            webSocketMsg += name;
            webSocketMsg += "\n";
        }
    }
}

static void sendFavorites(PsychicWebSocketClient *c = nullptr)
{
    webSocketMsg = "FAVORITES:\n";

    File dir;

    {
        ScopedMutex lock(sdMutex);
        dir = SD.open(FAVORITES_DIR);
    }

    if (!dir || !dir.isDirectory())
    {
        if (c)
            msgToClient(webSocketMsg.c_str(), c);
        else
            websocketHandler.sendAll(webSocketMsg.c_str());

        return;
    }

    handleFolder(dir);

    log_i("favorites webSocketMsg size: %d", webSocketMsg.length());

    if (c)
        msgToClient(webSocketMsg.c_str(), c);
    else
        websocketHandler.sendAll(webSocketMsg.c_str());
}

void favoritesTask(void *param)
{
    log_d("favoritesTask running");

    webSocketMsg.reserve(4096);

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
