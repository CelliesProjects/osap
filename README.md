# OS Audio Player

ESP32 + VS1053 based network and SD audio player with a responsive web interface.

A very capable audio player with:

* MP3, M4A, AAC, OGG, and 16-bit FLAC playback
* Playlist queue for 100 items
* WebSocket based live UI
* A very fast SD card filebrowser
* Radio presets 
* Automatic adding of premium/private channels during build
* Internet radio search with [radio-browser.info](https://www.radio-browser.info/)
* Favorites system
* Mobile friendly interface

## Mobile phone UI screenshot

![image](https://github.com/user-attachments/assets/6f5ec529-6386-45e2-9438-dc032500af85)

# Features

## Privacy first

* No cloud dependency
* No accounts
* No telemetry
* No phone app install
* No ads
* No “smart platform”
* Just a websocket UI on your LAN

## Audio 

* VS1053 hardware decoder with MP3, M4A, AAC, OGG and FLAC decoding
* Local SD playback
* Internet radio streaming
* Playlist queue system
* Favorites saving/loading
* Playback state synchronization between UI clients

## Web Interface

* Clean UI optimized for mobile use
* Responsive split-pane layout
* Works on all modern browsers
* Touch friendly controls
* Search interface for radio stations
* Overlay "now playing" mode
* Toast notifications and errors

### Multiple WiFi networks supported

The player can be configured with multiple WiFi networks and automatically connects to the strongest available known network.  
This allows the same device to be used with different network setups such as a home network and a mobile phone hotspot without requiring reconfiguration.

## UI Performance

Multiple simultaneous clients are supported and kept in sync with the player state.  

Search and file browser requests are handled async to keep the UI responsive.  

File browser example with a single client:

* The async backend handling and UI rendering of 100+ SD card items takes about ~2000ms
* While reading and playing 16-bit FLAC audio from the same SD card
* While the UI remains responsive

---

# Hardware

Minimal component count:

* [WEMOS S3 MINI](https://www.wemos.cc/en/latest/s3/s3_mini.html)
* [WEMOS Micro SD card shield](https://www.wemos.cc/en/latest/d1_mini_shield/micro_sd.html)
* [Adafruit VS1053 Codec + MicroSD Breakout](https://www.adafruit.com/product/1381)
* Optional [Adafruit 1.3" I²C OLED](https://www.adafruit.com/product/938) status indicator
* Web browser as main UI

## Optional status indicator

An optional [Adafruit 1.3" I²C OLED display](https://www.adafruit.com/product/938) can be added for device status information.  

It displays boot progress, connection status and the current IP address.  

**Note:** To keep the player unobtrusive and power efficient, the OLED automatically sleeps after startup.  
Wake-up is performed using a capacitive touch input, which can be as simple as a GPIO connected to a metal button, screw head or other exposed conductive surface.

## SPI Configuration Notes

The current implementation assumes dedicated SPI buses for SD card and VS1053 access.  
Shared SPI configurations may compile, partially function, fully function or fail in creative and confusing ways.

Dedicated SPI wiring is the recommended and supported configuration.

---

# Building

## Required secrets file

Before compiling create a new file with your WiFi and location secrets:

`src/secrets.hpp`

Use this example setup as a template:

```cpp
#pragma once

struct Secret
{
    const char *SSID;
    const char *PSK;
};

/* OSAP will connect to the strongest available network */
constexpr Secret networks[] =
    {
        {"network1", "password"},
        {"network2", "password"},
    };

/* Central European Time - see:
     https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv */
const char *TIMEZONE = "CET-1CEST,M3.5.0,M10.5.0/3";

/* Replace "nl" with your own country code:
    https://en.wikipedia.org/wiki/ISO_3166-2#Current_codes */
const char *NTP_POOL = "nl.pool.ntp.org";

/* Optional custom mDNS hostname */
//#define OSAP_HOSTNAME "music-player"
```

## Preset radio stations setup

Preset radio stations are defined in `src/presets.hpp`.

## Optional: adding private presets

Adding private or premium radio presets is very easy.

Place one or more `.pls` playlist files in the project root before compiling.  

During build:

* `.pls` files are automatically parsed
* presets are generated and merged into the main preset list

The added `.pls` files are ignored by git.

---

# Using the player

The web UI consists of two panes.  
A selectable source tab on the left and a playlist tab on the right.  

There are 4 source tabs, `library`, `presets`, `favorites` and `search`.

Click on a tab button to show a tab.

## Using the SD card library

You will need a FAT32 formatted micro SD card.  

Folders are navigated by clicking.

Single files can be added by clicking on the file or added and started with the **play** button.

Folders can be scanned and all found items added by clicking the **play** button.  

This will add all files in a folder and start playing the first added item if nothing is playing.  

## Favorites

Internet radio stations found through the search interface can be saved as favorites.

Favorites are stored on the SD card in the `/.favorites` folder.

Saved favorites can be inspected by visiting `http://player-ip/favorites` in a browser.

The generated files are formatted so they can easily be copied into `src/presets.hpp` if you want to make them permanent presets.

## Searching for radio stations

You can search for radio stations on [radio-browser.info](https://www.radio-browser.info/) with the search bar.  

Search results are displayed in the search tab.

## Now Playing overlay

The **Info** button at the bottom of the page toggles the *Now Playing* overlay.

The overlay shows the currently playing station or track and playback progress.  
After 30 seconds no activity the overlay is show automatically.  

When an Internet radio station originating from the search results is playing, a **Save as Favorite** button becomes available, allowing the station to be stored on the SD.

# Used Libraries / Components

## ESP32 C++ backend

These libraries are used internally by the player:

* HTTP traffic and WebSocket UI messaging are handled by [PsychicHttp](https://github.com/hoeken/PsychicHttp) webserver library - [MIT](https://github.com/hoeken/PsychicHttp?tab=MIT-1-ov-file)
* Audio playback is handled by [ESP32_VS1053_Stream](https://github.com/CelliesProjects/ESP32_VS1053_Stream) library - [MIT](https://github.com/CelliesProjects/ESP32_VS1053_Stream?tab=MIT-1-ov-file)
* Low level codec access is handled by [ESP_VS1053_Library](https://github.com/baldram/ESP_VS1053_Library) - [GPL-3.0](https://github.com/baldram/ESP_VS1053_Library?tab=GPL-3.0-1-ov-file)
* The OLED is handled by [Adafruit SSD1306 library](https://github.com/adafruit/Adafruit_SSD1306) - [BSD](https://github.com/adafruit/Adafruit_SSD1306?tab=License-1-ov-file)

## WebUI frontend

All frontend resources are compiled into the player UI:

* Vanilla HTML/CSS/JavaScript is used for the interface and application logic
* SVG icons from Google Fonts are inlined into the generated HTML during build - [Apache 2.0](https://github.com/google/material-design-icons?tab=Apache-2.0-1-ov-file#readme)
* [Reconnecting WebSocket](https://github.com/joewalnes/reconnecting-websocket) is included (minified) in the UI - [MIT](https://github.com/joewalnes/reconnecting-websocket?tab=MIT-1-ov-file)
* No frameworks, no runtime dependencies, no tracked downloads

---

# Project Status

* Hardware is feature complete, tested and stable
* No enclosure yet

Current focus:

* Designing a 3D printed hardware enclosure

![Dev hardware](https://github.com/user-attachments/assets/1dfe93a5-e581-4095-91f5-cb0e9c022061)

