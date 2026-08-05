#pragma once

#include <WiFi.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include "SystemState.hpp"
#include "gpio.hpp"

SystemState systemState = SystemState::BOOTING;

void oledMessage(SystemState state, const char *msg);
void oledTask(void *param);
