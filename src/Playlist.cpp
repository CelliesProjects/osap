#include "Playlist.hpp"

Playlist::Playlist()
    : _count(0), _current(-1)
{
}

bool Playlist::add(const PlaylistItem &item)
{
    if (_count >= MAX_PLAYLIST_ITEMS)
        return false;

    _items[_count++] = item;

    if (_count == 1)
        _current = 0;

    return true;
}

void Playlist::clear()
{
    _count = 0;
    _current = -1;
}

bool Playlist::next()
{
    if (_current + 1 >= _count)
        return false;

    _current++;
    return true;
}

bool Playlist::prev()
{
    if (_current <= 0)
        return false;

    _current--;
    return true;
}

bool Playlist::remove(uint8_t index)
{
    if (index >= _count)
        return false;

    for (uint8_t i = index; i < _count - 1; i++)
        _items[i] = _items[i + 1];

    _count--;

    if (_count == 0)
        _current = -1;
    else
    {
        if (_current > index)
            _current--;

        else if (_current >= _count && _current != -1)
            _current = _count - 1;
    }
    return true;
}

PlaylistItem *Playlist::currentItem()
{
    if (_current < 0 || _current >= _count)
        return nullptr;

    return &_items[_current];
}

bool Playlist::setCurrentPlaying(int8_t index)
{
    if (index < -1 || index >= _count)
        return false;

    _current = index;
    return true;
}

int8_t Playlist::currentPlaying() const
{
    return _current;
}

uint8_t Playlist::size() const
{
    return _count;
}

const PlaylistItem *Playlist::get(uint8_t index) const
{
    if (index >= _count)
        return nullptr;

    return &_items[index];
}