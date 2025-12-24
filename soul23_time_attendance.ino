#include <SPI.h>
#include <MFRC522.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <TimeLib.h> // Requiere instalar: arduino-cli lib install "Time"
#include <ESP8266WebServer.h>
#include <DNSServer.h>

#include "Config.h"

// Constantes globales de configuración
const char* timeServerUrl = "https://worldtimeapi.org/api/ip";
// const char* DEVICE_NAME = "Checador_Oficina_1"; // Eliminado, ahora es dinámico
const long timeZoneOffset = -21600;

// Intervalo de resincronización del reloj (1 hora)
const unsigned long syncInterval = 3600000; 

// ==========================================
// 🔌 PINES SEGUROS (NO INTERFIEREN CON BOOT)
// ==========================================
#define SS_PIN  D8  // GPIO 15
#define RST_PIN D0  // GPIO 16
#define OLED_SDA D2
#define OLED_SCL D1
#define LED_PIN  D3  // LED de estatus (GPIO 0) - Inicializado con pull-up para evitar problemas de boot
#define BUZZER_PIN D4 // Buzzer (GPIO 2) - ⚠️ IMPORTANTE: Inicializado MUY TARDE en setup() para evitar problemas de boot

MFRC522 mfrc522(SS_PIN, RST_PIN);

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Variables para el modo configuración
Config config;
ESP8266WebServer *server = nullptr;
DNSServer *dnsServer = nullptr;
bool configurationMode = false;
bool shouldRestart = false;

// Variables de control
unsigned long lastSyncTime = 0;
int lastMinuteDisplayed = -1;
bool hasError = false;
unsigned long lastBlinkTime = 0;
bool ledState = LOW;

// Control del Watchdog del Lector NFC
const unsigned long nfcCheckInterval = 15000; // 15 segundos para detección rápida de fallos
unsigned long lastNfcCheck = 0;


// ==========================================
// 📝 DECLARACIONES DE FUNCIONES
// ==========================================
void checkAndResetNFC();
void showStatus(String line1, String line2);
void syncTimeWithServer();
bool sendToWebhook(String uid, String name, String num_emp, String sucursal, String telegram_id);
void showClockScreen();
String getFormattedTime();
bool readCardData(byte *data, byte *length);
void processTag();
String generateUUID(byte length);
String getISO8601DateTime(time_t t);
String getFormattedDate();
void beep(int times, int duration);
void alarmSound();
void silenceBuzzer();
void updateStatusLED();
void scanI2C();
void startConfigurationMode();

// ==========================================
// 🚀 SETUP
// ==========================================
void setup() {
  Serial.println("\n--- Iniciando setup ---");
  // ⚠️ CRÍTICO: Inicializar GPIO 2 (BUZZER) PRIMERO para evitar ruido durante boot
  // GPIO 2 tiene pull-up interno que puede causar problemas con buzzer pasivo
  // Configurarlo como INPUT sin pull-up/pull-down para evitar interferencia
  pinMode(BUZZER_PIN, INPUT); // INPUT sin pull para evitar ruido durante boot
  delay(10); // Pequeña pausa para estabilizar
  
  // ⚠️ INICIALIZAR GPIO 0 (LED) CON PULL-UP para evitar problemas de boot
  // Si GPIO 0 está LOW durante boot, el ESP8266 entra en modo programación
  pinMode(LED_PIN, INPUT_PULLUP); // Primero como INPUT con pull-up interno
  delay(10); // Pequeña pausa para estabilizar
  
  Serial.begin(115200);
  delay(100);

  WiFi.persistent(false); // Evita la reconexión automática que causa conflictos

  Serial.println("\n\n--- Checador IOT (Public Version) ---");
  
  randomSeed(analogRead(A0));

  SPI.begin();
  Wire.begin(OLED_SDA, OLED_SCL);

  scanI2C();
  mfrc522.PCD_Init();
  mfrc522.PCD_DumpVersionToSerial();
  mfrc522.PCD_SetAntennaGain(mfrc522.RxGain_max); // 🚀 Maxima ganancia antena

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("Intentando dirección OLED 0x3D..."));
    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3D)) {
      Serial.println(F("Error: No se encuentra OLED"));
      sos();
    }
  }
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.dim(false);
  Serial.println("OLED inicializado y configurado.");

  if (!LittleFS.begin()) {
    Serial.println("LittleFS mount failed. Formatting...");
    if (LittleFS.format()) {
      Serial.println("LittleFS formatted successfully");
      if (LittleFS.begin()) {
        Serial.println("LittleFS mounted after format");
      } else {
        Serial.println("LittleFS mount failed after format");
      }
    } else {
      Serial.println("LittleFS format failed");
    }
  } else {
    Serial.println("LittleFS mounted successfully");
  }
  // --- Lógica de Doble Reset (Double Tap) ---
  uint32_t rtcMagic = 0;
  ESP.rtcUserMemoryRead(0, &rtcMagic, sizeof(rtcMagic));
  bool doubleResetDetected = (rtcMagic == 0x1A2B3C4D);

  // Armar el detector para el siguiente reset (se desarmará en loop después de 3s)
  rtcMagic = 0x1A2B3C4D;
  ESP.rtcUserMemoryWrite(0, &rtcMagic, sizeof(rtcMagic));

  // Verificar botón físico (Flash/D3) O Doble Reset
  bool forceConfigMode = (digitalRead(LED_PIN) == LOW) || doubleResetDetected;
  
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Serial.print("Modo Config forzado: ");
  if (digitalRead(LED_PIN) == LOW) Serial.print("BOTON ");
  if (doubleResetDetected) Serial.print("DOBLE_RESET ");
  Serial.println(forceConfigMode ? "(SI)" : "(NO)");

  bool configLoaded = loadConfig(config);
  Serial.print("Configuración cargada exitosamente: ");
  Serial.println(configLoaded ? "Sí" : "No");

  if (configLoaded && !forceConfigMode) {
    showStatus("CONECTANDO", "WIFI...");
    WiFi.mode(WIFI_STA);
    WiFi.begin(config.ssid, config.password);

    int timeout = 0;
    while (WiFi.status() != WL_CONNECTED && timeout < 60) {
      delay(500); Serial.print("."); timeout++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      showStatus("WIFI", "OK");
      delay(1000);
      configurationMode = false;
    } else {
      showStatus("ERROR WIFI", "MODO CONFIG");
      delay(2000);
      configurationMode = true;
    }
  } else {
    showStatus("HOLA!", "MODO CONFIG");
    delay(2000);
    configurationMode = true;
  }
  
  if (configurationMode) {
    startConfigurationMode();
  } else {
    syncTimeWithServer();
    Serial.println("Sistema Listo.");

    delay(100);
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);
    delay(50);
    digitalWrite(BUZZER_PIN, LOW);
    delay(10);
    digitalWrite(BUZZER_PIN, LOW);

    blinkLed(5, 200);
    digitalWrite(LED_PIN, LOW);
    silenceBuzzer();
  }
}

// ==========================================
// 🔄 LOOP PRINCIPAL
// ==========================================
void loop() {
  // Desarmar Doble Reset después de 3 segundos de funcionamiento estable
  static bool drDisarmed = false;
  if (!drDisarmed && millis() > 3000) {
      uint32_t rtcMagic = 0;
      ESP.rtcUserMemoryWrite(0, &rtcMagic, sizeof(rtcMagic));
      drDisarmed = true;
      // Serial.println("Double Reset: Desarmado"); // Opcional para debug
  }

  if (shouldRestart) {
    Serial.println(F("Reiniciando sistema..."));
    delay(2000);
    ESP.restart();
  }

  if (configurationMode) {
    if (dnsServer) dnsServer->processNextRequest();
    if (server) server->handleClient();
    delay(10);
  } else {
    checkAndResetNFC();

    if (timeStatus() == timeNotSet || (millis() - lastSyncTime > syncInterval)) {
      syncTimeWithServer();
    }

    if (minute() != lastMinuteDisplayed) {
      showClockScreen();
      lastMinuteDisplayed = minute();
    }

    if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
      processTag();
      lastMinuteDisplayed = -1;
    }

    silenceBuzzer();
    delay(50);
  }
}

// ==========================================
// 📋 LÓGICA DE PROCESAMIENTO
// ==========================================
void processTag() {
  String uid = "";
  for (byte i = 0; i < mfrc522.uid.size; i++) {
     uid += (mfrc522.uid.uidByte[i] < 0x10 ? "0" : "");
     uid += String(mfrc522.uid.uidByte[i], HEX);
  }
  uid.toUpperCase();

  byte buffer[128]; 
  byte bufferSize = sizeof(buffer);
  
  if (readCardData(buffer, &bufferSize)) {
    int jsonStart = -1;
    for (int i = 0; i < bufferSize; i++) { if (buffer[i] == '{') { jsonStart = i; break; } }

    if (jsonStart != -1) {
      DynamicJsonDocument docCard(512);
      DeserializationError error = deserializeJson(docCard, (const char*)(buffer + jsonStart));

      if (error) {
        showStatus("ERROR", "JSON ILEGIBLE");
        signalTemporaryError();
      } else {
        String name = docCard["name"];
        String num_emp = docCard["num_emp"];
        String sucursal = docCard["sucursal"];
        String telegram_id = docCard["telegram_id"];
        
        sendToWebhook(uid, name, num_emp, sucursal, telegram_id);
      }
    } else {
      showStatus("ERROR", "SIN JSON");
      signalTemporaryError();
    }
  } else {
    showStatus("ERROR", "LECTURA FALLO");
    signalTemporaryError();
  }

  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
  // El delay y el apagado del LED se manejan ahora en las funciones de señalización
}

// ==========================================
// 🚨 WATCHDOG DEL LECTOR NFC
// ==========================================
/**
 * @brief Verifica el estado del lector NFC y lo reinicia si no responde.
 * 
 * Se ejecuta periódicamente según 'nfcCheckInterval'. Lee el registro de versión
 * del chip MFRC522. Si devuelve 0x00 o 0xFF, asume que está colgado e intenta
 * reinicializarlo mediante un HARD RESET físico (Pin RST).
 */
void checkAndResetNFC() {
  if (millis() - lastNfcCheck > nfcCheckInterval) {
    // 1. Heartbeat check: Read Firmware Version
    byte v = mfrc522.PCD_ReadRegister(MFRC522::VersionReg);

    // Si la versión es 0x00 o 0xFF, la comunicación SPI o el módulo fallaron
    if (v == 0x00 || v == 0xFF) {
      Serial.println(F("NFC Watchdog: Lector NO responde. Ejecutando HARD RESET..."));

      // 2. Hard Reset Sequence (Physical Pin Toggle)
      // Forzamos el pin RST a LOW para reiniciar el chip físicamente
      pinMode(RST_PIN, OUTPUT);
      digitalWrite(RST_PIN, LOW);
      delay(50); // Mantener reset
      digitalWrite(RST_PIN, HIGH);
      delay(50); // Esperar a que el chip despierte

      // 3. Re-Inicializar Librería
      mfrc522.PCD_Init();
      delay(10);

      // 4. Boost Sensitivity (Max Gain) - Importante para estabilidad
      mfrc522.PCD_SetAntennaGain(mfrc522.RxGain_max);

      // 5. Verificar recuperación
      byte v_new = mfrc522.PCD_ReadRegister(MFRC522::VersionReg);
      if (v_new == 0x00 || v_new == 0xFF) {
         Serial.println(F("NFC Watchdog: FALLO. El lector sigue sin responder."));
      } else {
         Serial.print(F("NFC Watchdog: RECUPERADO. Versión: 0x"));
         Serial.println(v_new, HEX);
      }
    } 
    // Si responde bien, no hacemos nada para no interrumpir el flujo normal
    
    lastNfcCheck = millis();
  }
}

// ==========================================
// ☁️ RED Y LECTURA DE TARJETA
// ==========================================
/**
 * @brief Lee los datos almacenados en los bloques de la tarjeta MIFARE.
 * 
 * Intenta autenticarse con la clave por defecto (NFC Forum Key) y leer
 * los bloques 4 a 12 de la tarjeta.
 * 
 * @param data Puntero al buffer donde se guardarán los datos leídos.
 * @param length Puntero al tamaño del buffer (se actualiza con bytes leídos).
 * @return true Si la lectura fue exitosa.
 * @return false Si hubo error de autenticación o lectura.
 */
bool readCardData(byte *data, byte *length) {
  MFRC522::MIFARE_Key key;
  byte nfcForumKey[6] = {0xD3, 0xF7, 0xD3, 0xF7, 0xD3, 0xF7};
  memcpy(key.keyByte, nfcForumKey, 6);

  MFRC522::StatusCode status = mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, 4, &key, &(mfrc522.uid));
  if (status != MFRC522::STATUS_OK) { return false; }
  
  delay(5);

  byte readBlock[18];
  byte index = 0;
  for (byte block = 4; block < 12; block++) {
    if (block % 4 == 3) {
      status = mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, block + 1, &key, &(mfrc522.uid));
      if (status != MFRC522::STATUS_OK) break;
      continue;
    }
    byte size = sizeof(readBlock);
    status = mfrc522.MIFARE_Read(block, readBlock, &size);
    if (status != MFRC522::STATUS_OK) break;
    memcpy(&data[index], readBlock, 16);
    index += 16;
  }
  *length = index;
  return index > 0;
}

/**
 * @brief Sincroniza la hora interna del ESP8266 con un servidor NTP/API externo.
 * 
 * Realiza una petición HTTP GET a 'timeServerUrl' (WorldTimeAPI).
 * Parsea el JSON de respuesta y establece la hora del sistema usando 'TimeLib'.
 */
void syncTimeWithServer() {
  if (WiFi.status() != WL_CONNECTED) return;
  std::unique_ptr<WiFiClientSecure> client(new WiFiClientSecure);
  client->setInsecure();
  HTTPClient http;
  if (http.begin(*client, timeServerUrl)) {
    http.setTimeout(10000); // Timeout de 10 segundos para evitar resets por watchdog
    http.setUserAgent(config.deviceName);
    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
      DynamicJsonDocument doc(1024);
      deserializeJson(doc, http.getString());
      // Ajuste para el servidor de ejemplo worldtimeapi.org
      setTime(doc["unixtime"].as<unsigned long>() + timeZoneOffset); 
      lastSyncTime = millis();
    }
    http.end();
  }
}

/**
 * @brief Envía el registro de asistencia al Webhook configurado.
 * 
 * Construye un JSON con los datos del empleado y la tarjeta, y lo envía
 * mediante una petición HTTP POST segura (HTTPS).
 * 
 * @param uid UID de la tarjeta leída.
 * @param name Nombre del empleado.
 * @param num_emp Número de empleado.
 * @param sucursal Sucursal asignada.
 * @param telegram_id ID de Telegram para notificaciones.
 * @return true Si el envío fue exitoso (HTTP 2xx).
 * @return false Si hubo error en la conexión o respuesta.
 */
bool sendToWebhook(String uid, String name, String num_emp, String sucursal, String telegram_id) {
  if (timeStatus() == timeNotSet) {
    showStatus("ERROR", "SIN HORA");
    signalTemporaryError();
    return false;
  }
  
  std::unique_ptr<WiFiClientSecure> client(new WiFiClientSecure);
  client->setInsecure();
  HTTPClient http;
  
  unsigned long utcTimestamp = now() - timeZoneOffset;
  String uuid = generateUUID(11);
  String date = getFormattedDate();
  String datetime_utc = getISO8601DateTime(utcTimestamp);

  showStatus("ENVIANDO", "REGISTRO...");

  Serial.print("Webhook URL: "); Serial.println(config.webhookUrl);

  if (http.begin(*client, config.webhookUrl)) {
    http.setTimeout(15000); // Aumentar timeout a 15s
    http.setUserAgent(config.deviceName);
    http.addHeader("Content-Type", "application/json");

    DynamicJsonDocument doc(1024);
    JsonArray array = doc.to<JsonArray>();

    JsonObject obj = array.createNestedObject();
    obj["uuid"] = uuid;
    obj["timestamp"] = utcTimestamp;
    obj["datetime_utc"] = datetime_utc;
    obj["date"] = date;
    obj["num_empleado"] = num_emp;
    obj["name"] = name;
    obj["branch"] = sucursal;
    obj["telegram_id"] = telegram_id;

    String jsonPayload;
    serializeJson(doc, jsonPayload);
    Serial.print("Payload: "); Serial.println(jsonPayload); // Debug payload
    
    int httpResponseCode = http.POST(jsonPayload);
    Serial.print("HTTP Code: "); Serial.println(httpResponseCode); // Debug code

    if (httpResponseCode > 0) {
        String response = http.getString();
        Serial.println("Respuesta: " + response);
    } else {
        Serial.print("Error HTTP: ");
        Serial.println(http.errorToString(httpResponseCode));
    }

    if (httpResponseCode >= 200 && httpResponseCode < 300) {
      showStatus("HOLA :)", name);
      http.end();
      blinkLed(5, 200); // Éxito - Parpadeo de 5 veces (mismo comportamiento que boot)
      return true;
    } else {
      showStatus("ERROR", "AL ENVIAR");
      http.end();
      signalTemporaryError();
      return false;
    }
  }
  signalTemporaryError();
  return false;
}

// ==========================================
// 🔊 BUZZER Y LEDS
// ==========================================

void signalTemporaryError() {
  alarmSound();
  digitalWrite(LED_PIN, HIGH);
  delay(5000);
  digitalWrite(LED_PIN, LOW);
}

void blinkLed(int times, int duration) {
  for (int i = 0; i < times; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(duration);
    digitalWrite(LED_PIN, LOW);
    if (i < times - 1) delay(duration);
  }
}

void morseDot() {
  digitalWrite(LED_PIN, HIGH);
  delay(250);
  digitalWrite(LED_PIN, LOW);
  delay(250);
}

void morseDash() {
  digitalWrite(LED_PIN, HIGH);
  delay(750);
  digitalWrite(LED_PIN, LOW);
  delay(250);
}

void sos() {
  while(true) { // Bucle infinito para S.O.S.
    // SOS con la luz: ... --- ... (3 segundos)
    morseDot(); morseDot(); morseDot();
    delay(500);
    morseDash(); morseDash(); morseDash();
    delay(500);
    morseDot(); morseDot(); morseDot();
    delay(3000); // 3 segundos de SOS
    delay(10000); // Esperar 10 segundos antes de repetir
  }
}

void beep(int times, int duration) {
  // Asegurar que el pin esté configurado (por si se llama antes del setup completo)
  static bool buzzerInitialized = false;
  if (!buzzerInitialized) {
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);
    buzzerInitialized = true;
  }
  
  for (int i = 0; i < times; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(duration);
    digitalWrite(BUZZER_PIN, LOW);
    if (i < times - 1) delay(duration / 2);
  }
  
  // Asegurar que el buzzer quede silenciado después de cada beep
  digitalWrite(BUZZER_PIN, LOW);
  delay(10); // Pequeña pausa para estabilizar
}

void alarmSound() {
  beep(3, 150); // 3 beeps cortos y rápidos para el error
  silenceBuzzer(); // Asegurar que quede silenciado después del error
}

void silenceBuzzer() {
  // Función para asegurar que el buzzer esté completamente silenciado
  // Útil para evitar ruido o interferencia electromagnética
  // Asegurar que el pin esté configurado como OUTPUT y en LOW
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
}

// ==========================================
// 🖥️ PANTALLA Y UTILIDADES
// ==========================================
String generateUUID(byte length) {
  String randomString = "";
  const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
  for (byte i = 0; i < length; i++) {
    randomString += charset[random(sizeof(charset) - 1)];
  }
  return randomString;
}

String getISO8601DateTime(time_t t) {
  char buff[21];
  sprintf(buff, "%04d-%02d-%02dT%02d:%02d:%02dZ",
    year(t), month(t), day(t), hour(t), minute(t), second(t));
  return String(buff);
}

String getFormattedDate() {
  String dateStr = String(year()) + "-";
  if (month() < 10) dateStr += "0"; dateStr += String(month());
  dateStr += "-";
  if (day() < 10) dateStr += "0"; dateStr += String(day());
  return dateStr;
}

String getFormattedTime() {
  String timeStr = "";
  int h = hour(); int m = minute(); String ampm = "AM";
  if (h >= 12) { ampm = "PM"; if (h > 12) h -= 12; }
  if (h == 0) h = 12;
  if (h < 10) timeStr += " "; timeStr += String(h); timeStr += ":";
  if (m < 10) timeStr += "0"; timeStr += String(m); timeStr += " " + ampm;
  return timeStr;
}

void showClockScreen() {
  display.clearDisplay();
  display.setTextSize(1); display.setCursor(0, 0); display.println(F("CHECADOR"));
  display.setTextSize(2); display.setCursor(10, 25); 
  if (timeStatus() != timeNotSet) { display.println(getFormattedTime()); }
  else { display.println("--:--"); }
  display.setTextSize(1); display.setCursor(0, 55); display.println(F("ACERQUE TARJETA..."));
  display.display();
}

void showStatus(String line1, String line2) {
  display.clearDisplay();
  display.setTextSize(2); display.setCursor(0, 10); display.println(line1);
  if(line2.length() > 8) display.setTextSize(1); else display.setTextSize(2);
  display.setCursor(0, 35); display.println(line2);
  display.display();
}

void scanI2C() {
  byte error, address;
  int nDevices = 0;
  Serial.println("Dispositivos I2C encontrados:");
  for (address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();
    if (error == 0) {
      Serial.print("Encontrado en 0x");
      if (address < 16) Serial.print("0");
      Serial.println(address, HEX);
      nDevices++;
    } else if (error == 4) {
      Serial.print("Error desconocido en 0x");
      if (address < 16) Serial.print("0");
      Serial.println(address, HEX);
    }
  }
  if (nDevices == 0) Serial.println("Ningún dispositivo I2C encontrado");
  else Serial.println("Escaneo terminado");
}

// ==========================================
// 🌐 MODO CONFIGURACIÓN WEB
// ==========================================

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>Configuracion Checador</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { background-color: #1a1a1a; color: #f0f0f0; font-family: Arial, sans-serif; text-align: center; padding: 20px; }
        h1 { color: #00bcd4; }
        .container { max-width: 400px; margin: auto; background: #262626; padding: 20px; border-radius: 10px; box-shadow: 0 0 10px rgba(0,0,0,0.5); }
        input[type=text], input[type=password] { width: 90%; padding: 12px; margin: 8px 0; display: inline-block; border: 1px solid #444; border-radius: 4px; box-sizing: border-box; background: #333; color: #f0f0f0; }
        button { background-color: #008CBA; color: white; padding: 14px 20px; margin: 8px 0; border: none; border-radius: 4px; cursor: pointer; width: 95%; font-size: 16px; }
        button:hover { background-color: #005f7a; }
        label { text-align: left; display: block; margin-left: 5%; }
    </style>
</head>
<body>
    <div class="container">
        <h1>Configurar Checador</h1>
        <form action="/save" method="POST">
            <label for="ssid">SSID (WiFi)</label>
            <input type="text" id="ssid" name="ssid" required>
            <label for="password">Contraseña (WiFi)</label>
            <input type="password" id="password" name="password">
            <label for="webhookUrl">URL del Webhook</label>
            <input type="text" id="webhookUrl" name="webhookUrl" required>
            <label for="deviceName">Nombre del Dispositivo</label>
            <input type="text" id="deviceName" name="deviceName" placeholder="Ej: Oficina_Principal" required>
            <button type="submit">Guardar y Reiniciar</button>
        </form>
    </div>
</body>
</html>
)rawliteral";

const char saved_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>Guardado</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { background-color: #1a1a1a; color: #f0f0f0; font-family: Arial, sans-serif; text-align: center; padding-top: 50px; }
        .message { font-size: 24px; color: #00bcd4; }
    </style>
</head>
<body>
    <div class="message">
        <h1>Configuracion Guardada!</h1>
        <p>El dispositivo se reiniciara...</p>
    </div>
</body>
</html>
)rawliteral";

void startConfigurationMode() {
  Serial.println("DEBUG: Entering startConfigurationMode");
  const char* ap_ssid = "Soul23";
  const char* ap_password = "1234567890";

  showStatus("MODO CONFIG", ap_ssid);

  WiFi.disconnect(true);
  delay(1000);

  WiFi.mode(WIFI_AP);
  delay(100);
  WiFi.softAP(ap_ssid, ap_password);
  delay(100);

  IPAddress apIP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(apIP);

  if (!dnsServer) dnsServer = new DNSServer();
  dnsServer->start(53, "*", apIP);

  if (!server) server = new ESP8266WebServer(80);

  server->on("/", HTTP_GET, [](){
    server->send_P(200, "text/html", index_html);
  });

  server->on("/save", HTTP_POST, [](){
    if (server->hasArg("ssid") && server->hasArg("webhookUrl") && server->hasArg("deviceName")) {
        String ssidRecibido = server->arg("ssid");
        String passRecibido = server->hasArg("password") ? server->arg("password") : "";
        String urlRecibida = server->arg("webhookUrl");
        String deviceNameRecibido = server->arg("deviceName");

        strlcpy(config.ssid, ssidRecibido.c_str(), sizeof(config.ssid));
        strlcpy(config.webhookUrl, urlRecibida.c_str(), sizeof(config.webhookUrl));
        strlcpy(config.password, passRecibido.c_str(), sizeof(config.password));
        strlcpy(config.deviceName, deviceNameRecibido.c_str(), sizeof(config.deviceName));
        
        showStatus("SAVED", "REINICIANDO...");
        saveConfig(config);

        server->send_P(200, "text/html", saved_html);

        shouldRestart = true;
    } else {
        server->send(400, "text/plain", "Faltan datos");
    }
  });

  server->begin();
}

// ==========================================
// 💾 GESTIÓN DE CONFIGURACIÓN
// ==========================================
