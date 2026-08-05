#pragma once

#include <driver/gpio.h>

// SD card (HSPI)
constexpr gpio_num_t SD_SCK  = GPIO_NUM_12;
constexpr gpio_num_t SD_MISO = GPIO_NUM_13;
constexpr gpio_num_t SD_MOSI = GPIO_NUM_11;
constexpr gpio_num_t SD_CS   = GPIO_NUM_10;

// VS1053 (VSPI)
constexpr gpio_num_t VS1053_SCK  = GPIO_NUM_36;
constexpr gpio_num_t VS1053_MISO = GPIO_NUM_18;
constexpr gpio_num_t VS1053_MOSI = GPIO_NUM_35;
constexpr gpio_num_t VS1053_CS   = GPIO_NUM_33;

constexpr gpio_num_t VS1053_DCS  = GPIO_NUM_37;
constexpr gpio_num_t VS1053_DREQ = GPIO_NUM_38;

constexpr gpio_num_t VS1053_RST = GPIO_NUM_NC; // connected to esp32-s3 RST/EN

// OLED (I2C)
constexpr gpio_num_t OLED_SCL = GPIO_NUM_2;
constexpr gpio_num_t OLED_SDA = GPIO_NUM_16;
constexpr uint8_t OLED_BUS_ID = 0x3D;

// Touch sensor
constexpr gpio_num_t TOUCH_SENSOR = GPIO_NUM_4;
