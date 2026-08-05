#pragma once

#include <Arduino.h>

constexpr uint8_t MAX_PLAYLIST_ITEMS = 100;

enum class SourceType : uint8_t
{
    FILE,
    RADIO,
    FAVORITE,
    SEARCH
};

struct PlaylistItem
{
    SourceType type;
    String name;
    String url;
};

class Playlist
{
public:
    Playlist();

    bool add(const PlaylistItem &item);
    void clear();

    bool next();
    bool prev();

    bool remove(uint8_t index);

    PlaylistItem *currentItem();
    bool setCurrentPlaying(int8_t index);
    int8_t currentPlaying() const;
    uint8_t size() const;

    const PlaylistItem *get(uint8_t index) const;

private:
    PlaylistItem _items[MAX_PLAYLIST_ITEMS];
    uint8_t _count;
    int8_t _current;
};
