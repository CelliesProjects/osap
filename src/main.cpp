#include <Arduino.h>
#include <WiFiMulti.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <ESPmDNS.h>

#include "secrets.hpp"
#include "BrowserRequest.hpp"
#include "SearchRequest.hpp"
#include "FavoritesRequest.hpp"
#include "PlayerCmd.hpp"
#include "SystemState.hpp"
#include "gpio.hpp"

WiFiMulti wifiMulti;

SPIClass spiSD(HSPI);
SemaphoreHandle_t sdMutex = nullptr;
extern const char *FAVORITES_DIR;

QueueHandle_t playerQueue = nullptr;
QueueHandle_t browserQueue = nullptr;
QueueHandle_t searchQueue = nullptr;
QueueHandle_t favoritesQueue = nullptr;

extern void oledMessage(SystemState state, const char *msg);
extern void oledTask(void *param);
extern void playerTask(void *param);
extern void serverTask(void *param);
extern void browserTask(void *param);
extern void searchTask(void *param);

#ifndef OSAP_HOSTNAME
#define OSAP_HOSTNAME "osap"
#endif

void runWiFiMulti()
{
    if (WiFi.status() != WL_CONNECTED)
    {
        log_v("WiFi not connected! Reconnecting...");
        wifiMulti.run();
    }
}

static void setupMDNS()
{
    if (MDNS.begin(OSAP_HOSTNAME))
    {
        MDNS.addService("http", "tcp", 80);

        log_i("mdns started");

        return;
    }

    log_e("could not start mdns");
}

static void fatalError(const char *msg)
{
    log_e("FATAL ERROR! %s", msg);
    oledMessage(SystemState::ERROR, msg);
    delay(portMAX_DELAY);
}

static void createTask(TaskFunction_t fn, const char *name, uint32_t stack, UBaseType_t prio)
{
    if (xTaskCreate(fn, name, stack, nullptr, prio, nullptr) != pdPASS)
        fatalError(name);
}

static void setupNetworks()
{
    constexpr size_t NETWORK_COUNT = sizeof(networks) / sizeof(networks[0]);

    static_assert(NETWORK_COUNT > 0,
                  "At least one WiFi access point must be configured");

    static_assert(networks[0].SSID[0] != '\0',
                  "The first WiFi SSID may not be empty");

    wifiMulti.APlistClean();

    for (size_t curr = 0; curr < NETWORK_COUNT; curr++)
        wifiMulti.addAP(networks[curr].SSID, networks[curr].PSK);
}

void setup()
{
#if defined(CORE_DEBUG_LEVEL) && (CORE_DEBUG_LEVEL != ESP_LOG_NONE)
    Serial.begin(115200);

    log_i("Audioplayer version: %s", GIT_VERSION);
    log_i("ESP32 IDF Version %d.%d.%d", ESP_IDF_VERSION_MAJOR, ESP_IDF_VERSION_MINOR, ESP_IDF_VERSION_PATCH);
    log_i("ESP32 Arduino Version %d.%d.%d", ESP_ARDUINO_VERSION_MAJOR, ESP_ARDUINO_VERSION_MINOR, ESP_ARDUINO_VERSION_PATCH);
    log_i("CPU: %iMhz", getCpuFrequencyMhz());
#endif

    btStop(); /* switch off BlueTooth radio as it is not used in this app */

    Wire.setPins(OLED_SDA, OLED_SCL);
    Wire.begin();
    Wire.beginTransmission(OLED_BUS_ID);
    const uint8_t error = Wire.endTransmission();
    Wire.end(); /* allow Adafruit SSD1306 to reinitialize Wire cleanly */

    if (!error)
        createTask(oledTask, "oledTask", 4096, 0);

    browserQueue = xQueueCreate(5, sizeof(ListRequest));
    if (!browserQueue)
        fatalError("browser queue could not be created");

    playerQueue = xQueueCreate(10, sizeof(PlayerCmd));
    if (!playerQueue)
        fatalError("player queue could not be created");

    searchQueue = xQueueCreate(5, sizeof(SearchRequest));
    if (!searchQueue)
        fatalError("search queue could not be created");

    favoritesQueue = xQueueCreate(5, sizeof(FavoritesRequest));
    if (!favoritesQueue)
        fatalError("favorites queue could not be created");        

    sdMutex = xSemaphoreCreateMutex();
    if (!sdMutex)
        fatalError("sd mutex could not be created");

    if (!spiSD.begin(SD_SCK, SD_MISO, SD_MOSI))
        fatalError("sd spi pin error");

    if (!SD.begin(SD_CS, spiSD, 20000000))
    {
        log_e("card mount failed");
        oledMessage(SystemState::ERROR, "SD card not mounted");
    }
    else
        log_i("sd initialized");

    if (!SD.mkdir(FAVORITES_DIR))
        log_e("could not create %s", FAVORITES_DIR);

    WiFi.setAutoReconnect(false);
    WiFi.setSleep(false);

    setupNetworks();

    while (wifiMulti.run() != WL_CONNECTED)
        vTaskDelay(pdMS_TO_TICKS(3000));

    log_i("wifi connected to %s - ip: %s", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());

    configTzTime(TIMEZONE, NTP_POOL);

    struct tm timeinfo = {};
    while (!getLocalTime(&timeinfo, 0))
        delay(10);

    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);

    log_i("ntp synced at %s local time", buf);

    setupMDNS();

    if (!SPI.begin(VS1053_SCK, VS1053_MISO, VS1053_MOSI))
        fatalError("vs1053 spi pin error");

    createTask(playerTask, "playerTask", 1024 * 5, 7);
    createTask(browserTask, "browserTask", 4096, 0);
    createTask(searchTask, "searchTask", 1024 * 5, 0);
    createTask(serverTask, "serverTask", 4096, 5);

    vTaskDelete(nullptr);
}

void loop()
{
}
