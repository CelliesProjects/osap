#include "favoritesTask.hpp"

static FavoritesRequest req;

static void processItems(File &dir)
{
    cachedFavorites = "FAVORITES:\n";

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
            if (!file.available())
                break;

            String line;

            {
                ScopedMutex lock(sdMutex);
                line = file.readStringUntil('\n');
            }

            line.trim();

            if (line.startsWith("NAME:"))
            {
                name = line.substring(5);
                break;
            }
        }

        file.close();

        if (name.length())
        {
            cachedFavorites += name;
            cachedFavorites += "\n";
        }
    }
}

static void sendWS(PsychicWebSocketClient *client)
{
    if (client)
        msgToClient(cachedFavorites.c_str(), client);
    else
        websocketHandler.sendAll(cachedFavorites.c_str());
}

static void sendFavorites(PsychicWebSocketClient *client = nullptr)
{
    if (cachedFavorites.isEmpty())
    {
        log_i("favorites cache empty, rebuilding");

        File dir;

        {
            ScopedMutex lock(sdMutex);
            dir = SD.open(FAVORITES_DIR);
        }

        if (!dir || !dir.isDirectory())
        {
            websocketHandler.sendAll("ERROR:Could not open favorites");
            return;
        }

        processItems(dir);

        dir.close();

        log_d("cachedFavorites size: %d", cachedFavorites.length());
    }

    sendWS(client);
}

void favoritesTask(void *param)
{
    cachedFavorites.reserve(WS_MSG_RESERVED);

    while (1)
    {
        log_d("stack high water mark: %i", uxTaskGetStackHighWaterMark(NULL));

        if (xQueueReceive(favoritesQueue, &req, portMAX_DELAY) != pdTRUE)
            continue;

        const auto startMS = millis();

        sendFavorites(req.client);

        log_i("favorites: %d ms", millis() - startMS);
    }
}
