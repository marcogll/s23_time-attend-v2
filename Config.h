#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

struct Config {
    char ssid[32];
    char password[64];
    char webhookUrl[256];
    char deviceName[32];
};

void saveConfig(const Config& config);
bool loadConfig(Config& config);

#endif // CONFIG_H
