#include "searchTask.hpp"

static constexpr int MAX_ITEMS = 30;
static constexpr int MAX_SEARCHNAME_LENGTH = 72;
static constexpr int MAX_SANITIZED_LENGTH = 64;
static constexpr int TIMEOUT_MS = 2000;

static HTTPClient http;
static WiFiClientSecure client;
static SearchRequest req;
static char msgBuffer[512];
static String sanitized;

static HTTPClient resolverHttp;
static WiFiClientSecure resolverClient;

static constexpr uint32_t CACHE_TIME_S = 6UL * 60UL * 60UL;
static constexpr const char *USER_AGENT = "OSAudioPlayer/0.1 ESP32 +https://github.com/celliesprojects/osap";

static char apiServer[64] = "";
static uint32_t lastResolveTime = 0;
static int foundResults = 0;

static String sanitizeStationName(const char *input)
{
    sanitized.clear();

    while (*input)
    {
        char c = *input++;

        // remove apostrophes
        if (c == '\'')
            continue;

        // keep readable chars
        if (isalnum((unsigned char)c))
            sanitized += c;

        else if (c == ' ' || c == '-' || c == '+' || c == '_' || c == '.' || c == '[' || c == ']')
            sanitized += c;

        else
            sanitized += ' ';
    }

    // collapse duplicate spaces
    while (sanitized.indexOf("  ") != -1)
        sanitized.replace("  ", " ");

    sanitized.trim();

    if (sanitized.length() > MAX_SANITIZED_LENGTH)
        sanitized = sanitized.substring(0, MAX_SANITIZED_LENGTH);

    return sanitized;
}

static void composeResult(JsonDocument &doc, String &result)
{
    result.reserve(4096);

    snprintf(msgBuffer,
             sizeof(msgBuffer),
             "SEARCHRESULT:%d:%d\n",
             req.page,
             MAX_ITEMS);

    result += msgBuffer;

    const char US = 0x1F;

    std::vector<String> seen;

    JsonArray stations = doc.as<JsonArray>();

    for (JsonObject station : stations)
    {
        String name = sanitizeStationName(station["name"] | "Unknown");

        const char *streamUrl = station["url_resolved"] | "";
        const char *codec = station["codec"] | "";
        const bool hls = station["hls"] | false;
        const int bitrate = station["bitrate"] | 0;

        if (!streamUrl[0])
            continue;

        if (name.length() > MAX_SEARCHNAME_LENGTH)
            continue;

        if (hls)
            continue;

        const bool supportedCodec =
            strcasecmp(codec, "mp3") == 0 ||
            strcasecmp(codec, "aac") == 0 ||
            strcasecmp(codec, "aac+") == 0 ||
            strcasecmp(codec, "ogg") == 0;

        if (!supportedCodec)
            continue;

        String cleanUrl = streamUrl;

        if (cleanUrl.endsWith(".m3u8"))
            continue;

        int endOfUrl = cleanUrl.indexOf('&');

        if (endOfUrl != -1)
            cleanUrl = cleanUrl.substring(0, endOfUrl);

        bool duplicate = false;

        for (const auto &u : seen)
        {
            if (u == cleanUrl)
            {
                duplicate = true;
                break;
            }
        }

        if (duplicate)
            continue;

        seen.push_back(cleanUrl);

        result += name;
        result += US;
        result += cleanUrl;
        result += "\n";

        foundResults++;
    }
}

static const char *resolveRadioBrowserServer()
{
    const time_t now = time(nullptr);

    // check for valid cached server
    if (apiServer[0] && now >= lastResolveTime && (now - lastResolveTime) < CACHE_TIME_S)
        return apiServer;

    resolverHttp.setConnectTimeout(TIMEOUT_MS);

    if (!resolverHttp.begin(resolverClient, "https://all.api.radio-browser.info/json/servers"))
    {
        log_w("resolver connect failed");
        return apiServer[0] ? apiServer : nullptr;
    }

    resolverHttp.setUserAgent(USER_AGENT);
    resolverHttp.setTimeout(TIMEOUT_MS);

    const int code = resolverHttp.GET();
    if (code <= 0)
    {
        log_w("resolver failed: %s", HTTPClient::errorToString(code).c_str());
        resolverHttp.end();
        return apiServer[0] ? apiServer : nullptr;
    }

    if (code != HTTP_CODE_OK)
    {
        log_w("resolver returned error: %i", code);
        resolverHttp.end();
        return apiServer[0] ? apiServer : nullptr;
    }

    if (resolverHttp.getSize() < 1)
    {
        log_w("resolver returned empty response");
        resolverHttp.end();
        return apiServer[0] ? apiServer : nullptr;
    }

    JsonDocument doc;

    const DeserializationError err = deserializeJson(doc, resolverHttp.getStream());

    resolverHttp.end();

    if (err)
    {
        log_w("resolver json failed");
        return apiServer[0] ? apiServer : nullptr;
    }

    JsonArray arr = doc.as<JsonArray>();
    if (!arr.size())
    {
        log_w("resolver returned empty list");
        return apiServer[0] ? apiServer : nullptr;
    }

    const char *name = arr[0]["name"] | "";
    if (!name || !name[0])
    {
        log_w("resolver invalid entry");
        return apiServer[0] ? apiServer : nullptr;
    }

    strlcpy(apiServer, name, sizeof(apiServer));

    lastResolveTime = now;

    log_i("resolved api server: %s", apiServer);

    return apiServer;
}

void searchTask(void *param)
{
    log_d("searchTask running");

    sanitized.reserve(MAX_SANITIZED_LENGTH * 2);

    resolverClient.setInsecure();
    client.setInsecure();

    while (1)
    {
        log_v("stack high water mark: %i", uxTaskGetStackHighWaterMark(NULL));

        if (xQueueReceive(searchQueue, &req, portMAX_DELAY) != pdTRUE)
            continue;

        log_v("processing request for page=%d max %d items query='%s'",
              req.page,
              MAX_ITEMS,
              req.query);

        PsychicWebSocketClient *wsClient = websocketHandler.getClient(req.client);
        if (!wsClient)
            continue;

        const char *server = resolveRadioBrowserServer();
        if (!server)
        {
            msgToClient("ERROR:Search backend unavailable", wsClient);
            continue;
        }

        snprintf(msgBuffer, sizeof(msgBuffer), "MESSAGE:Started '%s' page '%i' search", req.query, req.page + 1);
        msgToClient(msgBuffer, wsClient);

        const int offset = req.page * MAX_ITEMS;
        const String encoded = urlEncode(req.query);

        snprintf(msgBuffer, sizeof(msgBuffer),
                 "https://%s/json/stations/search?"
                 "name=%s"
                 "&hidebroken=true"
                 "&order=votes"
                 "&reverse=true"
                 "&offset=%d"
                 "&limit=%d",
                 server,
                 encoded.c_str(),
                 offset,
                 MAX_ITEMS + 1);

        log_v("connecting to %s", msgBuffer);

        http.setConnectTimeout(TIMEOUT_MS);

        if (!http.begin(client, msgBuffer))
        {
            msgToClient("ERROR:Search could not connect", wsClient);
            continue;
        }

        http.setUserAgent(USER_AGENT);
        http.setTimeout(TIMEOUT_MS);

        const int code = http.GET();

        // network / transport error
        if (code <= 0)
        {
            http.end();

            snprintf(msgBuffer, sizeof(msgBuffer), "ERROR:Search failed: %s", HTTPClient::errorToString(code).c_str());
            msgToClient(msgBuffer, wsClient);
            continue;
        }

        // server unhappy
        if (code != HTTP_CODE_OK)
        {
            http.end();

            snprintf(msgBuffer, sizeof(msgBuffer), "ERROR:Search server returned HTTP %d", code);
            msgToClient(msgBuffer, wsClient);
            continue;
        }

        const String payload = http.getString();

        http.end();

        if (payload.isEmpty())
        {
            msgToClient("ERROR:Search returned no data", wsClient);
            continue;
        }

        log_v("search payload size: %u", payload.length());

        JsonDocument filter;

        filter[0]["name"] = true;
        filter[0]["url_resolved"] = true;
        filter[0]["codec"] = true;
        filter[0]["hls"] = true;
        filter[0]["bitrate"] = true;

        JsonDocument doc;

        const DeserializationError err =
            deserializeJson(doc,
                            payload,
                            DeserializationOption::Filter(filter));

        if (err)
        {
            snprintf(msgBuffer, sizeof(msgBuffer), "ERROR:Search parse failed: %s", err.c_str());
            msgToClient(msgBuffer, wsClient);
            continue;
        }

        String result;
        composeResult(doc, result);
        msgToClient(result.c_str(), wsClient);

        const bool multiPage = foundResults > MAX_ITEMS;

        foundResults = min(foundResults, MAX_ITEMS);

        snprintf(msgBuffer, sizeof(msgBuffer), "MESSAGE:Search returned %i%s results", foundResults, multiPage ? "+" : "");
        msgToClient(msgBuffer, wsClient);

        foundResults = 0;
    }
}
