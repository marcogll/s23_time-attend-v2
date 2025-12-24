#include "Config.h"

void saveConfig(const Config& config) {
  Serial.println("Guardando configuración...");
  DynamicJsonDocument doc(512);
  doc["ssid"] = config.ssid;
  doc["password"] = config.password;
  doc["webhookUrl"] = config.webhookUrl;
  doc["deviceName"] = config.deviceName;

  File configFile = LittleFS.open("/config.json", "w");
  if (!configFile) {
    Serial.println("Error: No se pudo abrir el archivo de configuración para escritura.");
    return;
  }

  serializeJson(doc, configFile);
  configFile.close();
  Serial.println("Configuración guardada exitosamente.");
}

bool loadConfig(Config& config) {
  Serial.println("Intentando cargar configuración...");
  if (LittleFS.exists("/config.json")) {
    Serial.println("Archivo 'config.json' encontrado.");
    File configFile = LittleFS.open("/config.json", "r");
    if (configFile) {
      String fileContent = configFile.readString();
      Serial.println("Contenido del archivo: " + fileContent);

      DynamicJsonDocument doc(512);
      DeserializationError error = deserializeJson(doc, fileContent);

      if (!error) {
        Serial.println("JSON parseado correctamente.");
        strlcpy(config.ssid, doc["ssid"], sizeof(config.ssid));
        strlcpy(config.password, doc["password"], sizeof(config.password));
        strlcpy(config.webhookUrl, doc["webhookUrl"], sizeof(config.webhookUrl));
        strlcpy(config.deviceName, doc["deviceName"] | "Checador_Generico", sizeof(config.deviceName));

        Serial.println("SSID cargado: " + String(config.ssid));
        Serial.println("Webhook URL cargado: " + String(config.webhookUrl));
        Serial.println("Device Name cargado: " + String(config.deviceName));

        configFile.close();
        return true;
      } else {
        Serial.print("Error al parsear JSON: ");
        Serial.println(error.c_str());
      }
      configFile.close();
    } else {
        Serial.println("Error: No se pudo abrir el archivo de configuración para lectura.");
    }
  } else {
    Serial.println("No se encontró el archivo 'config.json'.");
  }
  return false;
}
