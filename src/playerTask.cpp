#include "playerTask.hpp"

static constexpr const char *ERROR_PLAYLIST_FULL = "ERROR:Playlist full";
static constexpr int MAX_FILENAME_LENGTH = 100;

static ESP32_VS1053_Stream audio;
static Playlist playList;
static PlayerCmd _cmd;
static char msgBuffer[512];
static int currentVolume = 100;
static String streamTitle = "";
static uint32_t bitrate = 0;
static String codec = "";

static void feedDecoder()
{
    if (!audio.isRunning())
        return;

    const auto *item = playList.currentItem();
    const bool needsSDLock = item && item->type == SourceType::FILE;
    if (needsSDLock)
    {
        ScopedMutex lock(sdMutex);
        audio.loop();
    }
    else
        audio.loop();
}

static const char *sourceTypeToString(SourceType type)
{
    switch (type)
    {
    case SourceType::FILE:
        return "FILE";
    case SourceType::RADIO:
        return "RADIO";
    case SourceType::FAVORITE:
        return "FAVORITE";
    case SourceType::SEARCH:
        return "SEARCH";
    default:
        return "UNKNOWN";
    }
}

static void broadcastCurrent(int index)
{
    char msg[24];
    snprintf(msg, sizeof(msg), "CURRENT:%d", index);
    websocketHandler.sendAll(msg);
}

static void createPlaylistBody(String &msg)
{
    msg += "PLAYLIST:\n";

    const char US = 0x1F;

    for (uint8_t i = 0; i < playList.size(); i++)
    {
        const auto &item = playList.get(i);

        msg += i;
        msg += US;
        msg += item->name;
        msg += US;
        msg += sourceTypeToString(item->type);
        msg += "\n";
    }
}

static void clearCurrentPlaying()
{
    playList.setCurrentPlaying(-1);
    broadcastCurrent(-1);
}

static void stopPlayback()
{
    if (!audio.isRunning())
        return;

    ScopedMutex lock(sdMutex);
    audio.stopSong();
}

static bool playIndex(int index, size_t offset = 0)
{
    if (index < 0 || index >= playList.size())
    {
        log_e("invalid index");
        return false;
    }

    const auto *item = playList.get(index);

    if (!item)
    {
        log_e("playlist error");
        return false;
    }

    if (audio.isRunning())
    {
        ScopedMutex lock(sdMutex);
        audio.stopSong();
    }

    log_v("starting: %s", item->url.c_str());

    bool success = false;

    const bool isLocalFile = (item->type == SourceType::FILE);
    if (isLocalFile)
    {
        ScopedMutex lock(sdMutex);
        success = audio.connectToFile(SD, item->url.c_str(), offset);
    }
    else
        success = audio.connectToHost(item->url.c_str(), offset);

    if (!success)
        return false;

    log_v("index %d started", index);
    return true;
}

static bool startPlayingIndex(int index, size_t offset = 0)
{
    if (!playIndex(index, offset))
        return false;

    if (playList.currentPlaying() != index)
    {
        playList.setCurrentPlaying(index);
        broadcastCurrent(index);
    }

    return true;
}

static void broadcastPlaylist()
{
    String msg;
    msg.reserve(2048);

    createPlaylistBody(msg);

    websocketHandler.sendAll(msg.c_str());

    broadcastCurrent(playList.currentPlaying());
}

static void sendPlaylist(const PlayerCmd &cmd)
{
    String msg;
    msg.reserve(2048);

    createPlaylistBody(msg);

    msgToClient(msg.c_str(), cmd.client);

    msg = "CURRENT:";
    msg += playList.currentPlaying();

    msgToClient(msg.c_str(), cmd.client);
}

static void sendStreamTitle(const PlayerCmd &cmd)
{
    snprintf(msgBuffer, sizeof(msgBuffer), "STREAMTITLE:%s", streamTitle.c_str());
    msgToClient(msgBuffer, cmd.client);
}

static void sendPresets(const PlayerCmd &cmd)
{
    String msg;
    msg.reserve(2048);
    msg += "PRESETS:\n";

    for (uint8_t i = 0; i < NUMBER_OF_PRESETS; i++)
    {
        msg += preset[i].name;
        msg += "\n";
    }

    msgToClient(msg.c_str(), cmd.client);
}

String favoritesToCStruct()
{
    String out;
    out.reserve(4096);

    out += "constexpr struct source preset[]{\n\n";

    File dir;

    {
        ScopedMutex lock(sdMutex);
        dir = SD.open(FAVORITES_DIR);
    }

    if (!dir || !dir.isDirectory())
    {
        out += "};\n";
        return out;
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
        String url;

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
                name = line.substring(5);

            else if (line.startsWith("URL:"))
                url = line.substring(4);
        }

        {
            ScopedMutex lock(sdMutex);
            file.close();
        }

        if (!name.isEmpty() && !url.isEmpty())
        {
            out += "    {\"";
            out += name;
            out += "\", \"";
            out += url;
            out += "\"},\n";
        }
    }

    out += "\n};\n";
    return out;
}

static bool addPreset(const PlayerCmd &cmd)
{
    int index = cmd.index;

    if (index < 0 || index >= NUMBER_OF_PRESETS)
    {
        msgToClient("ERROR:Invalid index", cmd.client);
        return false;
    }

    if (!playList.add({SourceType::RADIO, preset[index].name, preset[index].url}))
    {
        msgToClient(ERROR_PLAYLIST_FULL, cmd.client);
        return false;
    }

    return true;
}

static void queueNextItem(int index)
{
    PlayerCmd next{};
    next.type = PlayerCmdType::NEXT;
    next.index = index;

    broadcastCurrent(index);

    if (xQueueSend(playerQueue, &next, 0) != pdTRUE)
        broadcastPlayerBusy();
}

static int addSingleFile(const PlayerCmd &cmd)
{
    const char *path = cmd.path;
    const char *name = strrchr(path, '/');
    name = name ? name + 1 : path;

    if (!playList.add({SourceType::FILE, name, path}))
    {
        msgToClient(ERROR_PLAYLIST_FULL, cmd.client);
        return 0;
    }

    return 1;
}

static int addMultipleFiles(const PlayerCmd &cmd)
{
    const char *path = cmd.path;
    std::vector<PlaylistItem> temp;
    temp.reserve(32); // optional and test!

    File dir;
    {
        ScopedMutex lock(sdMutex);
        dir = SD.open(path);
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

        if (!file.isDirectory())
            temp.push_back({SourceType::FILE, file.name(), file.path()});

        feedDecoder();
    }

    if (temp.empty())
    {
        msgToClient("ERROR:No files found", cmd.client);
        return 0;
    }

    std::sort(temp.begin(), temp.end(),
              [](const PlaylistItem &a, const PlaylistItem &b)
              {
                  return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
              });

    int added = 0;
    for (const auto &item : temp)
    {
        if (playList.add(item))
            added++;
    }

    if (added == 0)
    {
        msgToClient(ERROR_PLAYLIST_FULL, cmd.client);
        return 0;
    }

    return added;
}

static void addPath(const PlayerCmd &cmd)
{
    File f;
    {
        ScopedMutex lock(sdMutex);
        f = SD.open(cmd.path);
    }

    if (!f)
    {
        msgToClient("ERROR:Path not found", cmd.client);
        return;
    }

    const bool isDirectory = f.isDirectory();
    const int added = isDirectory ? addMultipleFiles(cmd) : addSingleFile(cmd);

    if (!added)
    {
        msgToClient("MESSAGE:Added 0 items", cmd.client);
        return;
    }

    broadcastPlaylist();

    const bool shouldStart = isDirectory ? !audio.isRunning() : (cmd.startNow || !audio.isRunning());
    const int index = playList.size() - added;
    const auto *item = playList.get(index);

    if (shouldStart)
    {
        if (1 == added)
            snprintf(msgBuffer, sizeof(msgBuffer), "MESSAGE:Starting '%s'", item->name.c_str());
        else
            snprintf(msgBuffer, sizeof(msgBuffer), "MESSAGE:Starting %d items", added);

        msgToClient(msgBuffer, cmd.client);

        if (startPlayingIndex(index))
        {
            playList.setCurrentPlaying(index);
            broadcastCurrent(index);
            return;
        }

        queueNextItem(index + 1);
        return;
    }

    if (1 == added)
        snprintf(msgBuffer, sizeof(msgBuffer), "MESSAGE:Added '%s'", item->name.c_str());
    else
        snprintf(msgBuffer, sizeof(msgBuffer), "MESSAGE:Added %d items", added);

    msgToClient(msgBuffer, cmd.client);
}

static bool addSearchItem(const PlayerCmd &cmd)
{
    PlaylistItem item;

    item.type = SourceType::SEARCH;
    item.name = cmd.name;
    item.url = cmd.path;

    if (!playList.add(item))
    {
        msgToClient(ERROR_PLAYLIST_FULL, cmd.client);
        return false;
    }

    return true;
}

static bool removeIndex(const PlayerCmd &cmd)
{
    if (!playList.remove(cmd.index))
    {
        msgToClient("ERROR:Could not delete", cmd.client);
        return false;
    }

    return true;
}

static void clearPlaylist(const PlayerCmd &cmd)
{
    if (audio.isRunning())
    {
        ScopedMutex lock(sdMutex);
        audio.stopSong();
    }

    playList.clear();
}

static String favoritePath(const String &name)
{
    String filename = name;

    filename.trim();

    if (filename.length() > MAX_FILENAME_LENGTH)
        filename = filename.substring(0, MAX_FILENAME_LENGTH);

    return FAVORITES_DIR + filename + ".fav";
}

static bool saveFavorite(const PlayerCmd &cmd)
{
    const auto *item = playList.currentItem();

    if (!item)
    {
        msgToClient("ERROR:Invalid item", cmd.client);
        return false;
    }

    if (item->type != SourceType::SEARCH &&
        item->type != SourceType::RADIO)
    {
        msgToClient("ERROR:Cannot favorite item", cmd.client);
        return false;
    }

    String path = favoritePath(item->name);

    String contents;
    contents.reserve(512);

    contents += "NAME:";
    contents += item->name;
    contents += "\nURL:";
    contents += item->url;
    contents += "\nTYPE:";
    contents += sourceTypeToString(item->type);
    contents += "\n";

    File f;

    feedDecoder();

    {
        ScopedMutex lock(sdMutex);

        f = SD.open(path, FILE_WRITE, true);
    }

    feedDecoder();

    if (!f)
    {
        msgToClient("ERROR:Could not save favorite", cmd.client);
        return false;
    }

    {
        ScopedMutex lock(sdMutex);

        f.print(contents);
        f.close();
    }

    feedDecoder();

    return true;
}

static bool loadFavorite(const char *name, PlaylistItem &item)
{
    if (!name || !name[0])
        return false;

    String path = FAVORITES_DIR;
    path += name;
    path += ".fav";

    File file;
    {
        ScopedMutex lock(sdMutex);
        file = SD.open(path);
    }

    if (!file || file.isDirectory())
    {
        log_w("favorite not found: %s", path.c_str());
        return false;
    }

    String favName;
    String favUrl;
    String favType;

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
            favName = line.substring(5);

        else if (line.startsWith("URL:"))
            favUrl = line.substring(4);

        else if (line.startsWith("TYPE:"))
            favType = line.substring(5);

        feedDecoder();
    }

    {
        ScopedMutex lock(sdMutex);
        file.close();
    }

    if (favName.isEmpty() || favUrl.isEmpty())
    {
        log_w("favorite malformed: %s", path.c_str());
        return false;
    }

    item.type = SourceType::FAVORITE;
    item.name = favName;
    item.url = favUrl;

    return true;
}

static bool addFavorite(const PlayerCmd &cmd)
{
    PlaylistItem item;

    if (!loadFavorite(cmd.name, item))
    {
        msgToClient("ERROR:Could not load favorite", cmd.client);
        return false;
    }

    if (!playList.add(item))
    {
        msgToClient(ERROR_PLAYLIST_FULL, cmd.client);
        return false;
    }

    return true;
}

static bool deleteFavorite(const PlayerCmd &cmd)
{
    String path = favoritePath(cmd.name);

    bool success = false;

    {
        ScopedMutex lock(sdMutex);
        success = SD.remove(path);
    }

    if (!success)
    {
        msgToClient("ERROR:Could not delete favorite", cmd.client);
        return false;
    }

    return true;
}

static int scaleVolume(int uiVolume)
{
    uiVolume = constrain(uiVolume, 0, 100);
    return !uiVolume ? 0 : map(uiVolume, 0, 100, 60, 100);
}

static void handlePlayerCommand(const PlayerCmd &cmd)
{
    if (!audio.isChipConnected())
    {
        log_v("dropping request");
        msgToClient("ERROR:No decoder found", cmd.client);
        return;
    }

    log_d("current playing index %d", playList.currentPlaying());

    const char *ERROR_FAVORITES_BUSY = "ERROR:Favorites queue full";

    switch (cmd.type)
    {

    case PlayerCmdType::DELETE_FAVORITE:
        if (deleteFavorite(cmd))
        {
            FavoritesRequest req{};
            req.client = nullptr; // nullptr triggers a broadcast to all clients

            if (xQueueSend(favoritesQueue, &req, 0) != pdTRUE)
                msgToClient(ERROR_FAVORITES_BUSY, cmd.client);
        }
        break;

    case PlayerCmdType::SAVE_FAVORITE:
    {
        if (!saveFavorite(cmd))
            break;

        msgToClient("MESSAGE:Saved as favorite", cmd.client);

        FavoritesRequest req{};
        req.client = nullptr; // nullptr triggers a broadcast to all clients

        if (xQueueSend(favoritesQueue, &req, 0) != pdTRUE)
            msgToClient(ERROR_FAVORITES_BUSY, cmd.client);
        break;
    }

    case PlayerCmdType::SEND_PRESETS:
        sendPresets(cmd);
        break;

    case PlayerCmdType::SEND_PLAYLIST:
        sendPlaylist(cmd);
        break;

    case PlayerCmdType::CLEAR_PLAYLIST:
        clearPlaylist(cmd);
        broadcastPlaylist();
        break;

    case PlayerCmdType::SET_VOLUME:
    {
        const int requested = constrain(cmd.volume, 0, 100);
        const int scaled = scaleVolume(requested);

        if (scaled == scaleVolume(currentVolume))
        {
            log_v("volume is the same, aborting");
            break;
        }

        audio.setVolume(scaled);

        currentVolume = requested;

        log_v("[AUDIO] audio set to %d by ui and applied scaled to %d for hardware", requested, scaled);

        snprintf(msgBuffer, sizeof(msgBuffer), "VOLUMESET:%d", currentVolume);
        websocketHandler.sendAll(msgBuffer);

        break;
    }

    case PlayerCmdType::SEND_VOLUME:
    {
        snprintf(msgBuffer, sizeof(msgBuffer), "VOLUMESET:%d", currentVolume);
        msgToClient(msgBuffer, cmd.client);
        break;
    }

        /* depending on current play state */

    case PlayerCmdType::EOF_REACHED:
    {
        const int current = playList.currentPlaying();

        if (current < 0)
        {
            log_w("EOF received without active item");
            break;
        }

        queueNextItem(current + 1);
        break;
    }

    case PlayerCmdType::PLAY_INDEX:
    {
        PlayerCmd next{};
        next.type = PlayerCmdType::NEXT;
        next.index = cmd.index;

        broadcastCurrent(cmd.index);

        const auto &item = playList.get(cmd.index);

        snprintf(msgBuffer, sizeof(msgBuffer), "MESSAGE:Starting '%s'", item->name.c_str());
        msgToClient(msgBuffer, cmd.client);

        if (xQueueSend(playerQueue, &next, 0) != pdTRUE)
            msgToClient(ERROR_PLAYER_BUSY, cmd.client);

        break;
    }

    case PlayerCmdType::REMOVE_INDEX:
    {
        const int index = cmd.index;
        const int current = playList.currentPlaying();
        const bool wasPlaying = (current == index);

        if (!removeIndex(cmd))
            break;

        if (wasPlaying)
        {
            stopPlayback();

            if (index < playList.size())
            {
                clearCurrentPlaying();

                if (!startPlayingIndex(index))
                    queueNextItem(index + 1);
            }
            else
                clearCurrentPlaying();
        }
        else if (index < current)
            playList.setCurrentPlaying(current - 1);

        broadcastPlaylist();
        break;
    }

    case PlayerCmdType::ADD_PRESET:
    {
        if (!addPreset(cmd))
            break;

        const int index = playList.size() - 1;
        const auto &item = playList.get(index);

        if (cmd.startNow || !audio.isRunning())
        {
            playList.setCurrentPlaying(index);
            broadcastPlaylist();
            snprintf(msgBuffer, sizeof(msgBuffer), "MESSAGE:Starting '%s'", item->name.c_str());
            msgToClient(msgBuffer, cmd.client);

            if (!startPlayingIndex(index))
                clearCurrentPlaying();

            break;
        }

        broadcastPlaylist();
        snprintf(msgBuffer, sizeof(msgBuffer), "MESSAGE:Added '%s'", item->name.c_str());
        msgToClient(msgBuffer, cmd.client);
        break;
    }

    case PlayerCmdType::ADD_PATH:
        addPath(cmd);
        break;

    case PlayerCmdType::NEXT:
    {
        const int index = cmd.index;

        if (index < 0 || index >= playList.size())
        {
            log_v("end of playlist");
            clearCurrentPlaying();
            broadcastPlaylist();
            break;
        }

        if (!startPlayingIndex(index))
            queueNextItem(index + 1);

        break;
    }

    case PlayerCmdType::PLAY_SEARCH:
    case PlayerCmdType::ADD_SEARCH:
    {
        if (!addSearchItem(cmd))
            break;

        const int index = playList.size() - 1;
        const auto &item = playList.get(index);

        if (cmd.type == PlayerCmdType::PLAY_SEARCH || !audio.isRunning())
        {
            snprintf(msgBuffer, sizeof(msgBuffer), "MESSAGE:Starting '%s'", item->name.c_str());
            msgToClient(msgBuffer, cmd.client);

            playList.setCurrentPlaying(index);
            broadcastPlaylist();

            if (!startPlayingIndex(index))
                clearCurrentPlaying();

            break;
        }

        snprintf(msgBuffer, sizeof(msgBuffer), "MESSAGE:Added '%s'", item->name.c_str());
        msgToClient(msgBuffer, cmd.client);
        broadcastPlaylist();

        break;
    }

    case PlayerCmdType::ADD_FAVORITE:
    case PlayerCmdType::PLAY_FAVORITE:
    {
        if (!addFavorite(cmd))
            break;

        const int index = playList.size() - 1;
        const auto &item = playList.get(index);

        if (cmd.type == PlayerCmdType::PLAY_FAVORITE || !audio.isRunning())
        {
            playList.setCurrentPlaying(index);
            broadcastPlaylist();
            snprintf(msgBuffer, sizeof(msgBuffer), "MESSAGE:Starting '%s'", item->name.c_str());
            msgToClient(msgBuffer, cmd.client);

            if (!startPlayingIndex(index))
            {
                playList.setCurrentPlaying(-1);
                broadcastCurrent(-1);
            }

            break;
        }

        broadcastPlaylist();
        snprintf(msgBuffer, sizeof(msgBuffer), "MESSAGE:Added '%s'", item->name.c_str());
        msgToClient(msgBuffer, cmd.client);

        break;
    }

    case PlayerCmdType::JUMPTO:
    {
        const int current = playList.currentPlaying();

        if (current < 0)
            break;

        const size_t size = audio.size();

        if (!size)
            break;

        if (cmd.position >= size)
            break;

        log_v("seeking to %zu", cmd.position);

        startPlayingIndex(current, cmd.position);

        break;
    }

    case PlayerCmdType::SEND_STREAMTITLE:
        if (!streamTitle.isEmpty())
        {
            snprintf(msgBuffer, sizeof(msgBuffer), "STREAMTITLE:%s", streamTitle.c_str());
            msgToClient(msgBuffer, cmd.client);
        }
        break;

    case PlayerCmdType::SEND_CODEC:
        if (!codec.isEmpty())
        {
            snprintf(msgBuffer, sizeof(msgBuffer), "CODEC:%s", codec);
            msgToClient(msgBuffer, cmd.client);
        }
        break;

    case PlayerCmdType::SEND_BITRATE:
        if (audio.isRunning() && bitrate)
        {
            snprintf(msgBuffer, sizeof(msgBuffer), "BITRATE:%lu kbps", bitrate);
            msgToClient(msgBuffer, cmd.client);
        }
        break;

    case PlayerCmdType::FLUSH_PLAYLIST:
    {
        const auto *item = playList.currentItem();

        if (!item)
        {
            log_w("flush playlist without active item");
            break;
        }

        PlaylistItem playing = *item;

        playList.clear();

        if (!playList.add(playing))
        {
            log_e("flush playlist: could not add current item");
            clearCurrentPlaying();
            broadcastPlaylist();
            break;
        }

        playList.setCurrentPlaying(0);
        broadcastPlaylist();

        break;
    }

    default:
        log_w("unhandled cmd type %d", static_cast<int>(cmd.type));
    }
}

static void eofCB(const char *info)
{
    PlayerCmd cmd{};
    cmd.type = PlayerCmdType::EOF_REACHED;

    if (xQueueSend(playerQueue, &cmd, 0) != pdTRUE)
        broadcastPlayerBusy();
}

static void codecCB(const char *name)
{
    codec = name;
    snprintf(msgBuffer, sizeof(msgBuffer), "CODEC:%s", codec);
    websocketHandler.sendAll(msgBuffer);
}

static void bitRateCB(uint32_t br)
{
    bitrate = br;
    snprintf(msgBuffer, sizeof(msgBuffer), "BITRATE:%lu kbps", bitrate);
    websocketHandler.sendAll(msgBuffer);
}

static void infoCB(const char *info)
{
    normalizeToUtf8(info, strlen(info), msgBuffer, sizeof(msgBuffer));

    streamTitle = msgBuffer;
    snprintf(msgBuffer, sizeof(msgBuffer), "STREAMTITLE:%s", streamTitle.c_str());
    websocketHandler.sendAll(msgBuffer);
}

static void errorCB(const char *error)
{
    snprintf(msgBuffer, sizeof(msgBuffer), "ERROR:%s", error);
    websocketHandler.sendAll(msgBuffer);
}

void playerTask(void *param)
{
    if (!audio.startDecoder(VS1053_CS, VS1053_DCS, VS1053_DREQ) || !audio.isChipConnected())
    {
        oledMessage(SystemState::ERROR, "No vs1053 codec found");
        log_e("[AUDIO] vs1053 board could not init");
    }
    else
    {
        if (SystemState::ERROR != systemState)
        {
            oledMessage(SystemState::RUNNING, "Ready to rock!");
            log_i("[AUDIO] Ready to rock!");
        }
    }

    log_v("[AUDIO] audio memory size: %zu bytes", sizeof(audio));

    audio.setEofCB(eofCB);
    audio.setCodecCB(codecCB);
    audio.setBitrateCB(bitRateCB);
    audio.setInfoCB(infoCB);
    audio.setErrorCB(errorCB);
    audio.setVolume(scaleVolume(currentVolume));

    while (1)
    {
        log_v("[AUDIO] stack high water mark: %i", uxTaskGetStackHighWaterMark(NULL));

        if (xQueueReceive(playerQueue, &_cmd, pdMS_TO_TICKS(1)) == pdTRUE)
            handlePlayerCommand(_cmd);

        if (audio.isRunning())
        {
            static constexpr auto UPDATE_FREQ_HZ = 4;
            static constexpr auto DELAY = 1000 / UPDATE_FREQ_HZ;

            static size_t prevUsed = -1;
            static unsigned long prevMs = 0;

            size_t used, capacity;
            audio.bufferStatus(used, capacity);

            if (prevUsed != used && (millis() - prevMs > DELAY))
            {
                log_v("[AUDIO] buffer status: %zu, %zu", used, capacity);

                snprintf(msgBuffer, sizeof(msgBuffer), "BUFFER:%zu:%zu", used, capacity);
                websocketHandler.sendAll(msgBuffer);

                prevUsed = used;
                prevMs = millis();
            }
        }

        if (audio.isRunning() && audio.size())
        {
            static constexpr auto UPDATE_FREQ_HZ = 4;
            static constexpr auto DELAY = 1000 / UPDATE_FREQ_HZ;

            static size_t prevPos = -1;
            static unsigned long prevMs = 0;

            const size_t curPos = audio.position();
            if (prevPos != curPos && (millis() - prevMs > DELAY))
            {
                log_v("[AUDIO] position: %zu", curPos);

                snprintf(msgBuffer, sizeof(msgBuffer), "PROGRESS:%zu:%zu", curPos, audio.size());
                websocketHandler.sendAll(msgBuffer);

                prevPos = curPos;
                prevMs = millis();
            }
        }

        feedDecoder();

        if (!audio.isRunning() && !codec.isEmpty())
        {
            log_v("[AUDIO] stopped - cleaning up");
            codec.clear();
            bitrate = 0;
            streamTitle = "";
        }

        static time_t prevHeartbeat = 0;
        const time_t now = time(nullptr);
        if (now != prevHeartbeat)
        {
            websocketHandler.sendAll("PING:");
            prevHeartbeat = now;
        }
    }
}
