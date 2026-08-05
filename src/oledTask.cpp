#include "oledTask.hpp"

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

static Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire);

static constexpr uint32_t TOUCH_THRESHOLD = 30100;
static constexpr uint32_t VISIBLE_TIME_MS = 15000;

static bool displayAwake = true;
static unsigned long wakeTimeMs = 0;
static char msgBuffer[512];

static bool uiRefreshRequested = false;
static char displayMessage[64] = {0};

void oledMessage(SystemState state, const char *msg)
{
    if (!wakeTimeMs) // OLED not present or not initialized yet.
        return;
    systemState = state;
    snprintf(displayMessage, sizeof(displayMessage), "%s", msg);
    uiRefreshRequested = true;
}

static void refreshUI()
{
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("OS Audio Player");
    display.setCursor(0, 10);

    snprintf(msgBuffer, sizeof(msgBuffer), "IP: %s", WiFi.isConnected() ? WiFi.localIP().toString().c_str() : "Not connected");
    display.println(msgBuffer);

    switch (systemState)
    {
    case SystemState::BOOTING:
        display.setCursor(0, 20);
        display.println("System is booting");
        break;

    case SystemState::RUNNING:
        display.setCursor(0, 20);
        display.println(displayMessage);
        break;

    case SystemState::ERROR:
        display.setCursor(0, 30);
        display.println("BOOT ERROR:");
        display.setCursor(0, 40);
        display.println(displayMessage);
        break;
    }

    display.display();
}

static void wifiEvent(WiFiEvent_t event)
{
    switch (event)
    {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
        uiRefreshRequested = true;
        break;

    default:
        break;
    }
}

static void handleTouch()
{
    const uint32_t value = touchRead(TOUCH_SENSOR);
    if (value > TOUCH_THRESHOLD)
    {
        log_v("wake touch = %lu", value);
        display.ssd1306_command(SSD1306_DISPLAYON);
        displayAwake = true;
        wakeTimeMs = millis();
    }
}

static void handleDisplayTimeout()
{
    if (millis() - wakeTimeMs >= VISIBLE_TIME_MS)
    {
        display.ssd1306_command(SSD1306_DISPLAYOFF);
        displayAwake = false;
    }
}

void oledTask(void *param)
{
    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_BUS_ID))
    {
        log_e("SSD1306 failed to init. oledTask deleted");
        vTaskDelete(nullptr);
    }

    log_i("SSD1306 running");

    display.dim(true);
    display.ssd1306_command(SSD1306_SETCONTRAST);
    display.ssd1306_command(0);

    display.setFont();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    WiFi.onEvent(wifiEvent);

    refreshUI();

    wakeTimeMs = millis();

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(25));

        if (uiRefreshRequested)
        {
            uiRefreshRequested = false;
            refreshUI();
        }

        if (!displayAwake)
        {
            handleTouch();
            continue;
        }

        handleDisplayTimeout();
    }
}
