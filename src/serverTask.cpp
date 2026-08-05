#include "serverTask.hpp"

static PsychicHttpServer server;
static const char *etagValue = "\"" GIT_VERSION "\"";
static const char *contentCreationTime = BUILD_LAST_MODIFIED;

static const char *TEXT_HTML = "text/html";
static const char *CONTENT_ENCODING = "Content-Encoding";
static const char *ENCODING_GZIP = "gzip";

static char msgBuffer[512];

void msgToClient(const char *msg, PsychicWebSocketClient *c)
{
    PsychicWebSocketClient *client = websocketHandler.getClient(c);
    if (client)
        client->sendMessage(msg);
}

void broadcastPlayerBusy()
{
    log_v("playerQueue full");
    websocketHandler.sendAll(ERROR_PLAYER_BUSY);
}

static inline bool samePageIsCached(PsychicRequest *request)
{
    const char *IF_MODIFIED_SINCE = "If-Modified-Since";
    const char *IF_NONE_MATCH = "If-None-Match";

    bool modifiedSince = request->hasHeader(IF_MODIFIED_SINCE) && request->header(IF_MODIFIED_SINCE).equals(contentCreationTime);
    bool noneMatch = request->hasHeader(IF_NONE_MATCH) && request->header(IF_NONE_MATCH).equals(etagValue);

    return modifiedSince || noneMatch;
}

static void addStaticContentHeaders(PsychicResponse *response)
{
    response->addHeader("Last-Modified", contentCreationTime);
    response->addHeader("ETag", etagValue);
}

static esp_err_t favoritesHandler(PsychicRequest *request, PsychicResponse *response)
{
    String html;
    html.reserve(2048);

    html += R"(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<title>OS Audio Player Favorites</title>
<link rel="icon" href="data:image/svg+xml,%3Csvg%20xmlns%3D%22http%3A//www.w3.org/2000/svg%22%20height%3D%2224px%22%20viewBox%3D%220%20-960%20960%20960%22%20width%3D%2224px%22%20fill%3D%22%230%22%3E%3Cpath%20d%3D%22m380-300%20280-180-280-180v360ZM480-80q-83%200-156-31.5T197-197q-54-54-85.5-127T80-480q0-83%2031.5-156T197-763q54-54%20127-85.5T480-880q83%200%20156%2031.5T763-763q54%2054%2085.5%20127T880-480q0%2083-31.5%20156T763-197q-54%2054-127%2085.5T480-80Z%22/%3E%3C/svg%3E">
</head>
<body>
<pre>
)";

    html += favoritesToCStruct();

    html += R"(
</pre>
</body>
</html>
)";

    response->setContentType("text/html");
    response->setContent(html.c_str());

    return response->send();
}

static void webserverUrlSetup()
{
    server.on("/", HTTP_GET, [](PsychicRequest *request, PsychicResponse *response)
              {
        if (samePageIsCached(request)) return response->send(304);

        extern const uint8_t index_start[] asm("_binary_src_generated_index_html_gz_start");
        extern const uint8_t index_end[] asm("_binary_src_generated_index_html_gz_end");                

        addStaticContentHeaders(response);
        response->addHeader(CONTENT_ENCODING, ENCODING_GZIP);
        response->setContentType(TEXT_HTML);
        response->setContent(index_start, index_end - index_start);
        return response->send(); });

    server.on("/favorites", HTTP_GET, favoritesHandler);

    server.onNotFound([](PsychicRequest *request, PsychicResponse *response)
                      {
        log_e("404 - Not found: 'http://%s%s'", request->host().c_str(), request->url().c_str());
        return response->send(404, TEXT_HTML, "<h1>404 Page not found</h1>"); });
}

static void presetToPlaylist(PsychicWebSocketRequest *request, const char *payload)
{
    const auto client = websocketHandler.getClient(request->client()->socket());
    const char *sub = payload + 7;

    int index;
    bool startItem = false;
    const char *numStart = nullptr;

    if (strncmp(sub, "PLAY:", 5) == 0)
    {
        numStart = sub + 5;
        startItem = true;
    }
    else if (strncmp(sub, "ADD:", 4) == 0)
        numStart = sub + 4;
    else
    {
        msgToClient("ERROR:Invalid preset subcommand", client);
        return;
    }

    char *endptr;
    index = strtol(numStart, &endptr, 10);

    if (*endptr != '\0')
    {
        msgToClient("ERROR:Invalid index format", client);
        return;
    }

    if (index < 0 || index >= NUMBER_OF_PRESETS)
    {
        msgToClient("ERROR:Invalid index", client);
        return;
    }

    PlayerCmd cmd{};
    cmd.type = PlayerCmdType::ADD_PRESET;
    cmd.index = index;
    cmd.startNow = startItem;
    cmd.client = client;

    if (xQueueSend(playerQueue, &cmd, 0) != pdTRUE)
    {
        msgToClient(ERROR_PLAYER_BUSY, client);
        return;
    }
}

static void libraryToPlaylist(PsychicWebSocketRequest *request, const char *payload)
{
    const auto client = websocketHandler.getClient(request->client()->socket());
    const char *sub = payload + 8;

    bool doPlay = false;
    const char *path = nullptr;

    if (strncmp(sub, "PLAY:", 5) == 0)
    {
        doPlay = true;
        path = sub + 5;
    }
    else if (strncmp(sub, "ADD:", 4) == 0)
    {
        path = sub + 4;
    }
    else
    {
        msgToClient("ERROR:Invalid library command", client);
        return;
    }

    if (!path || strlen(path) == 0)
    {
        msgToClient("ERROR:Empty path", client);
        return;
    }

    log_d("LIB path: %s", path);

    PlayerCmd cmd{};
    cmd.type = PlayerCmdType::ADD_PATH;
    cmd.startNow = doPlay;
    cmd.client = client;

    snprintf(cmd.path, sizeof(cmd.path), "%s", path);

    if (xQueueSend(playerQueue, &cmd, 0) != pdTRUE)
        msgToClient(ERROR_PLAYER_BUSY, client);
}

static void handleDelete(PsychicWebSocketRequest *request, const char *payload)
{
    const auto client = websocketHandler.getClient(request->client()->socket());
    const char *sub = payload + 7;

    if (strncmp(sub, "PLAYLIST:", 9) == 0)
    {
        PlayerCmd cmd{};
        cmd.type = PlayerCmdType::CLEAR_PLAYLIST;
        cmd.client = client;

        if (xQueueSend(playerQueue, &cmd, 0) != pdTRUE)
        {
            msgToClient(ERROR_PLAYER_BUSY, client);
            return;
        }

        return;
    }

    else if (strncmp(sub, "INDEX:", 6) == 0)
    {
        char *endptr;
        int index = strtol(sub + 6, &endptr, 10);

        if (*endptr != '\0')
        {
            msgToClient("ERROR:Invalid index format", client);
            return;
        }

        PlayerCmd cmd{};
        cmd.type = PlayerCmdType::REMOVE_INDEX;
        cmd.index = index;
        cmd.client = client;

        if (xQueueSend(playerQueue, &cmd, 0) != pdTRUE)
            msgToClient(ERROR_PLAYER_BUSY, client);

        return;
    }

    msgToClient("ERROR:Invalid delete command", client);
}

static void handlePlayIndex(PsychicWebSocketRequest *request, const char *payload)
{
    const auto client = websocketHandler.getClient(request->client()->socket());
    const char *sub = payload + 11;

    char *endptr;
    int index = strtol(sub, &endptr, 10);

    if (*endptr != '\0')
    {
        msgToClient("ERROR:Invalid index format", client);
        return;
    }

    PlayerCmd cmd{};
    cmd.type = PlayerCmdType::PLAY_INDEX;
    cmd.index = index;
    cmd.client = client;

    if (xQueueSend(playerQueue, &cmd, 0) != pdTRUE)
        msgToClient(ERROR_PLAYER_BUSY, client);
}

static void startSearch(PsychicWebSocketRequest *request, const char *payload)
{
    const auto client = websocketHandler.getClient(request->client()->socket());
    log_d("payload: %s", payload);

    const char *sub = payload + 7;

    char *endptr;
    int page = strtol(sub, &endptr, 10);

    if (endptr == sub || *endptr != ':')
    {
        msgToClient("ERROR:Invalid search format", client);
        return;
    }

    const char *query = endptr + 1;

    if (!query || !query[0])
    {
        msgToClient("ERROR:Empty search query", client);
        return;
    }

    log_d("SEARCH parsed -> page=%d query='%s'", page, query);

    SearchRequest req{};
    req.page = page;
    req.client = websocketHandler.getClient(request->client()->socket());

    snprintf(req.query, sizeof(req.query), "%s", query);

    if (xQueueSend(searchQueue, &req, 0) != pdTRUE)
        msgToClient("ERROR:Search queue busy", client);
}

static void searchToPlaylist(PsychicWebSocketRequest *request, const char *payload)
{
    const auto client = websocketHandler.getClient(request->client()->socket());

    // ---- determine command type ----
    const char *sub = nullptr;

    PlayerCmd cmd{};

    if (strncmp(payload + 7, "ADD:", 4) == 0)
    {
        cmd.type = PlayerCmdType::ADD_SEARCH;
        sub = payload + 11; // SEARCH:ADD:
    }
    else if (strncmp(payload + 7, "PLAY:", 5) == 0)
    {
        cmd.type = PlayerCmdType::PLAY_SEARCH;
        sub = payload + 12; // SEARCH:PLAY:
    }
    else
    {
        msgToClient("ERROR:Invalid search command", client);
        return;
    }

    if (!sub || !sub[0])
    {
        msgToClient("ERROR:Invalid search item", client);
        return;
    }

    const char US = 0x1F;

    const char *sep = strchr(sub, US);

    if (!sep)
    {
        msgToClient("ERROR:Malformed search item", client);
        return;
    }

    String name = String(sub).substring(0, sep - sub);

    const char *url = sep + 1;

    if (name.isEmpty() || !url[0])
    {
        msgToClient("ERROR:Invalid search item", client);
        return;
    }

    cmd.client = client;

    snprintf(cmd.name, sizeof(cmd.name), "%s", name.c_str());

    snprintf(cmd.path, sizeof(cmd.path), "%s", url);

    if (xQueueSend(playerQueue, &cmd, 0) != pdTRUE)
        msgToClient(ERROR_PLAYER_BUSY, client);
}

static void handleSearch(PsychicWebSocketRequest *request, const char *payload)
{
    // SEARCH:ADD:url
    if (strncmp(payload + 7, "ADD:", 4) == 0)
    {
        log_d("add url %s", payload);
        searchToPlaylist(request, payload);
        return;
    }

    // SEARCH:PLAY:url
    if (strncmp(payload + 7, "PLAY:", 5) == 0)
    {
        log_d("play url %s", payload);
        searchToPlaylist(request, payload);
        return;
    }

    // SEARCH:page:query
    startSearch(request, payload);
}

static void handleBrowseRequest(PsychicWebSocketRequest *request, const char *payload)
{
    ListRequest req{};
    req.client = websocketHandler.getClient(request->client()->socket());

    snprintf(req.path, sizeof(req.path), "%s", payload + 9);

    if (xQueueSend(browserQueue, &req, 0) != pdTRUE)
    {
        log_v("browserQueue full");
        msgToClient("ERROR:browserQueue full", req.client);
    }
}

static void handleFavorite(PsychicWebSocketRequest *request, const char *payload)
{
    const auto client = websocketHandler.getClient(request->client()->socket());

    PlayerCmd cmd{};

    const char *sub = payload + 9;

    if (strncmp(sub, "SAVE:", 5) == 0)
    {
        cmd.type = PlayerCmdType::SAVE_FAVORITE;
        cmd.index = atoi(sub + 5);
    }
    else if (strncmp(sub, "PLAY:", 5) == 0)
    {
        cmd.type = PlayerCmdType::PLAY_FAVORITE;
        snprintf(cmd.name, sizeof(cmd.name), "%s", sub + 5);
    }
    else if (strncmp(sub, "ADD:", 4) == 0)
    {
        cmd.type = PlayerCmdType::ADD_FAVORITE;
        snprintf(cmd.name, sizeof(cmd.name), "%s", sub + 4);
    }
    else if (strncmp(sub, "DELETE:", 7) == 0)
    {
        cmd.type = PlayerCmdType::DELETE_FAVORITE;
        snprintf(cmd.name, sizeof(cmd.name), "%s", sub + 7);
        msgToClient("MESSAGE:Deleting favorite",client);
    }
    else
    {
        msgToClient("ERROR:Invalid favorite command", client);
        return;
    }

    cmd.client = client;

    if (xQueueSend(playerQueue, &cmd, 0) != pdTRUE)
        msgToClient(ERROR_PLAYER_BUSY, client);
}

static void wsOpenHandler(PsychicWebSocketClient *client)
{
    log_d("[socket] connection #%u connected from %s",
          client->socket(),
          client->remoteIP().toString().c_str());

    log_i("active websocket clients: %u", websocketHandler.count());

    PlayerCmd cmd{};
    cmd.client = websocketHandler.getClient(client->socket());

    const auto requests =
        {
            PlayerCmdType::SEND_VOLUME,
            PlayerCmdType::SEND_PLAYLIST,
            PlayerCmdType::SEND_STREAMTITLE,
        };

    for (const auto &req : requests)
    {
        cmd.type = req;
        if (xQueueSend(playerQueue, &cmd, 0) != pdTRUE)
            msgToClient(ERROR_PLAYER_BUSY, cmd.client);
    }
}

static void wsCloseHandler(PsychicWebSocketClient *client)
{
    log_d("[socket] connection #%u closed from %s", client->socket(), client->remoteIP().toString().c_str());
}

static esp_err_t wsFrameHandler(PsychicWebSocketRequest *request, httpd_ws_frame *frame)
{
    if (frame->len == 0)
        return ESP_OK;

    const char *payload = reinterpret_cast<const char *>(frame->payload);

    if (strncmp(payload, "REQ:LIST:", 9) == 0)
    {
        handleBrowseRequest(request, payload);
        return ESP_OK;
    }

    else if (strncmp(payload, "REQ:PRESETS", 11) == 0)
    {
        PlayerCmd cmd{};
        cmd.type = PlayerCmdType::SEND_PRESETS;
        cmd.client = websocketHandler.getClient(request->client()->socket());

        if (xQueueSend(playerQueue, &cmd, 0) != pdTRUE)
            broadcastPlayerBusy();

        return ESP_OK;
    }

    else if (strncmp(payload, "REQ:FAVORITES", 13) == 0)
    {
        PlayerCmd cmd{};
        cmd.type = PlayerCmdType::SEND_FAVORITES;
        cmd.client = websocketHandler.getClient(request->client()->socket());

        if (xQueueSend(playerQueue, &cmd, 0) != pdTRUE)
            broadcastPlayerBusy();

        return ESP_OK;
    }

    else if (strncmp(payload, "SETVOLUME:", 10) == 0)
    {
        PlayerCmd cmd{};
        cmd.type = PlayerCmdType::SET_VOLUME;
        cmd.volume = atoi(payload + 10);
        cmd.client = websocketHandler.getClient(request->client()->socket());

        if (xQueueSend(playerQueue, &cmd, 0) != pdTRUE)
            msgToClient(ERROR_PLAYER_BUSY, cmd.client);

        return ESP_OK;
    }

    else if (strncmp(payload, "PRESET:", 7) == 0)
    {
        presetToPlaylist(request, payload);
        return ESP_OK;
    }

    else if (strncmp(payload, "LIBRARY:", 8) == 0)
    {
        libraryToPlaylist(request, payload);
        return ESP_OK;
    }

    else if (strncmp(payload, "DELETE:", 7) == 0)
    {
        handleDelete(request, payload);
        return ESP_OK;
    }

    else if (strncmp(payload, "PLAY:INDEX:", 11) == 0)
    {
        handlePlayIndex(request, payload);
        return ESP_OK;
    }

    else if (strncmp(payload, "SEARCH:", 7) == 0)
    {
        handleSearch(request, payload);
        return ESP_OK;
    }

    else if (strncmp(payload, "FAVORITE:", 9) == 0)
    {
        handleFavorite(request, payload);
        return ESP_OK;
    }

    else if (strncmp(payload, "NEXT:", 5) == 0)
    {
        PlayerCmd cmd{};
        cmd.type = PlayerCmdType::NEXT;

        if (xQueueSend(playerQueue, &cmd, 0) != pdTRUE)
            broadcastPlayerBusy();

        return ESP_OK;
    }

    else if (strncmp(payload, "JUMPTO:", 7) == 0)
    {
        PlayerCmd cmd{};
        cmd.type = PlayerCmdType::JUMPTO;
        cmd.position = atoi(payload + 7);
        cmd.client = websocketHandler.getClient(request->client()->socket());

        if (xQueueSend(playerQueue, &cmd, 0) != pdTRUE)
            broadcastPlayerBusy();

        return ESP_OK;
    }

    else if (strncmp(payload, "OSAM:HELLO", 10) == 0)
    {
        log_i("osam client connected");
        PlayerCmd cmd{};
        cmd.client = websocketHandler.getClient(request->client()->socket());

        cmd.type = PlayerCmdType::SEND_CODEC;
        if (xQueueSend(playerQueue, &cmd, 0) != pdTRUE)
            broadcastPlayerBusy();

        cmd.type = PlayerCmdType::SEND_BITRATE;
        if (xQueueSend(playerQueue, &cmd, 0) != pdTRUE)
            broadcastPlayerBusy();

        return ESP_OK;
    }

    log_w("unhandled payload %s", payload);
    return ESP_OK;
}

void serverTask(void *param)
{
    server.config.max_uri_handlers = 4;
    server.config.max_open_sockets = 12;
    server.config.lru_purge_enable = true;

    webserverUrlSetup();

    websocketHandler.onOpen(wsOpenHandler);
    websocketHandler.onClose(wsCloseHandler);
    websocketHandler.onFrame(wsFrameHandler);
    server.on("/ws", &websocketHandler);

    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");

    server.begin();

    while (1)
    {
        runWiFiMulti();
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}
