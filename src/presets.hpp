#pragma once

#include <Arduino.h>

struct source
{
    const char *name;
    const char *url;
};

constexpr struct source preset[]{

#ifdef USE_PRIVATE_PRESETS
#include "generated/private_presets.inc"
#endif

    {"NPO Radio1", "http://icecast.omroep.nl/radio1-bb-mp3"},
    {"NPO Radio2", "http://icecast.omroep.nl/radio2-bb-mp3"},
    {"NPO 3FM", "http://icecast.omroep.nl/3fm-bb-mp3"},
    {"NPO Klassiek", "http://icecast.omroep.nl/radio4-bb-mp3"},
    {"NPO Radio5", "http://icecast.omroep.nl/radio5-bb-mp3"},
    {"NPO Soul&Jazz", "http://icecast.omroep.nl/radio6-bb-mp3"},
    {"Radio Gelderland", "http://d2od87akyl46nm.cloudfront.net/icecast/omroepgelderland/radiogelderland"},
    {"DELTA RADIO NIJMEGEN", "http://streamdelta.lokaalradio.nl:9005/download.mp3"},
    {"Olympia Classics", "http://streams.olympia-streams.nl/classics192"},
    {"XXL Stenders", "http://streams.robstenders.nl:8063/bonanza_mp3"},
    {"192 Radio Nederland", "http://192radio.stream-server.nl/stream"},
    {"80s Hitradio Amsterdam", "http://s22.myradiostream.com:7728/"},
    {"Amsterdam Funk Channel", "https://stream.afc.fm/"},
    {"Jazz Radio Soul Food by DJ Philgood", "http://jazz-wr12.ice.infomaniak.ch/jazz-wr12-128.mp3"},
    {"Disco Mix", "https://play.discomix.ro/8002/stream"},
    {"Absoluut FM", "http://absoluutfm.stream.laut.fm/absoluutfm"},
    {"Record DiscoFunk", "https://radiorecord.hostingradio.ru/discofunk96.aacp"},
    {"RadioEins", "http://radioeins.de/stream"},
    {"ZUN radio", "https://icecast9.play.cz/zun192.mp3"},
    {"Tekno1", "https://tekno1.radioca.st/listen.pls"},
    {"Techno on Radio", "https://0n-techno.radionetz.de/0n-techno.aac"},
    {"Record TECHNO", "https://radiorecord.hostingradio.ru/techno96.aacp"},
    {"Kaaosradio 24h", "http://stream.kaaosradio.fi:8000/stream2"},
    {"bollywooddance", "https://nl4.mystreaming.net/uber/bollywooddance/icecast.audio"},
    {"bollywood", "http://2.mystreaming.net:80/er/bollywood/icecast.audio"},
};

constexpr const uint8_t NUMBER_OF_PRESETS = sizeof(preset) / sizeof(source);
