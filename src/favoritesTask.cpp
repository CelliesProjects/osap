#include "favoritesTask.hpp"

static FavoritesRequest req;
static String webSocketMsg;

static void processItems(File &dir)
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

static void sendWS(PsychicWebSocketClient *client)
{
    if (client)
        msgToClient(webSocketMsg.c_str(), client);
    else
        websocketHandler.sendAll(webSocketMsg.c_str());
}

static void sendFavorites(PsychicWebSocketClient *client = nullptr)
{
    webSocketMsg = "FAVORITES:\n";

    File dir;

    {
        ScopedMutex lock(sdMutex);
        dir = SD.open(FAVORITES_DIR);
    }

    if (!dir || !dir.isDirectory())
    {
        sendWS(client);
        return;
    }

    processItems(dir);

    log_d("favorites webSocketMsg size: %d", webSocketMsg.length());

    sendWS(client);
}

void favoritesTask(void *param)
{
    webSocketMsg.reserve(WS_MSG_RESERVED);

    while (1)
    {
        log_d("stack high water mark: %i", uxTaskGetStackHighWaterMark(NULL));

        if (xQueueReceive(favoritesQueue, &req, portMAX_DELAY) != pdTRUE)
            continue;

        sendFavorites(req.client);
    }
}
