<p align="center">
  <img src="https://raw.githubusercontent.com/marcogll/mg_data_storage/refs/heads/main/soul23/logo/soul23_logo.svg" width="110" alt="Soul23">
</p>

<h1 align="center">S23 Time Attend V2</h1>

<p align="center">
  Sistema de control de asistencia y jornada ⏰
</p>

<p align="center">
  
</p>

  Sistema de control de asistencia y jornada ⏰
</p>

<p align="center">
  
</p>

## ⚡️ Características Destacadas

*   **🛡️ Robustez Industrial:**
    *   **NFC Watchdog:** Verificación cada 15s. Si el lector se congela, el sistema realiza un *Hard Reset* físico al módulo RC522 y fuerza la ganancia de antena al máximo.
    *   **Auto-Formato:** Gestión automática del sistema de archivos (`LittleFS`) si se detecta corrupción.
    *   **Anti-Bloqueo:** Uso de `ESP8266WebServer` síncrono y gestión de memoria optimizada (PROGMEM) para evitar reinicios por desbordamiento de pila.

*   **⚙️ Configuración Vía Web (Portal Cautivo):**
    *   No requiere editar código (`secrets.h`) para cambiar WiFi o URLs.
    *   Configura **SSID**, **Password**, **Webhook URL** y **Nombre del Dispositivo** (User-Agent) desde el navegador.

*   **🎮 Controles Físicos y Gestuales:**
    *   **Doble Reset (Double Tap):** Presiona el botón `RST` dos veces rápido para forzar el Modo Configuración sin abrir la carcasa.
    *   **Botón Físico:** Opción de forzar config manteniendo presionado el botón (GPIO 0 / Flash) al arrancar.

---

## 🚀 Guía de Inicio Rápido

### 1. Hardware Requerido
| Componente | Descripción |
| :--- | :--- |
| **NodeMCU v2/v3** | ESP8266 (ESP-12E) |
| **RC522** | Lector NFC (SPI) |
| **OLED 0.96"** | Pantalla SSD1306 (I2C) |
| **Buzzer** | Activo/Pasivo (3.3V) |
| **LED** | Indicador de estado |

### 2. Carga del Firmware
Usa el script automatizado para compilar y subir (requiere `arduino-cli`):
```bash
./compile_and_upload.sh
```

### 3. Configuración Inicial
1.  Al encender por primera vez, el dispositivo creará una red WiFi llamada:
    *   **SSID:** `Soul23`
    *   **Password:** `1234567890`
2.  Conéctate con tu móvil o PC.
3.  Accede a `http://192.168.4.1` en tu navegador.
4.  Ingresa los datos:
    *   **SSID / Password:** Tu red WiFi local.
    *   **Webhook URL:** Endpoint donde recibirás los registros (ej. `https://api.tuempresa.com/asistencia`).
    *   **Nombre del Dispositivo:** Identificador único (se envía como `User-Agent` y en el JSON).
5.  Dale a "Guardar y Reiniciar". ¡Listo!

---

## 🔌 Diagrama de Conexión (Pinout Seguro)

Este pinout evita conflictos con el proceso de arranque (Boot) del ESP8266.

| RC522 | NodeMCU | GPIO | Notas |
| :--- | :--- | :--- | :--- |
| **SDA (SS)** | D8 | GPIO15 | Pull-down interno |
| **SCK** | D5 | GPIO14 | SPI Clock |
| **MOSI** | D7 | GPIO13 | SPI MOSI |
| **MISO** | D6 | GPIO12 | SPI MISO |
| **RST** | D0 | GPIO16 | **CRÍTICO:** Usado para Hard Reset del lector |
| **3.3V** | 3V3 | - | ⚠️ No usar 5V |
| **GND** | GND | - | - |

| Periférico | NodeMCU | GPIO | Notas |
| :--- | :--- | :--- | :--- |
| **OLED SDA** | D2 | GPIO4 | I2C Data |
| **OLED SCL** | D1 | GPIO5 | I2C Clock |
| **Buzzer** | D4 | GPIO2 | Inicializado tarde en setup() |
| **LED / Botón** | D3 | GPIO0 | Flash Button (Pull-up) |

---

## 🧠 Funcionamiento del Watchdog NFC

El lector RC522 es propenso a congelarse por interferencia electromagnética. Este firmware implementa una solución de 3 capas:

1.  **Monitorización:** Cada 15 segundos se consulta la versión del firmware del chip RC522.
2.  **Detección:** Si responde `0x00` o `0xFF`, se considera fallo.
3.  **Recuperación:**
    *   Se pone el pin `RST` (D0) en `LOW` por 50ms (corte de energía lógico).
    *   Se restaura a `HIGH`.
    *   Se re-inicializa la librería y se fuerza la ganancia de antena a `Max (48dB)`.

---

## 📡 Payloads

### Entrada (En Tarjeta NFC)
El tag debe contener un registro NDEF con un JSON válido:
```json
{
  "name": "Ana Lopez",
  "num_emp": "EMP001",
  "sucursal": "Centro",
  "telegram_id": "12345"
}
```

### Salida (Webhook POST)
El dispositivo envía este JSON al servidor:
```json
{
  "uuid": "AbC123XyZ",
  "timestamp": 1703435600,
  "datetime_utc": "2025-12-24T12:00:00Z",
  "date": "2025-12-24",
  "num_empleado": "EMP001",
  "name": "Ana Lopez",
  "branch": "Centro",
  "telegram_id": "12345"
}
```
*Headers:* `User-Agent`: (Nombre configurado en el portal)

---

## 🗺️ Roadmap de Desarrollo

### 📍 Fase 1: Optimización de Transmisión (Corto Plazo)
*   **Payload Base64:** Migrar el formato de envío de JSON plano a su equivalente codificado en **Base64**. Esto mejorará la integridad de los datos y facilitará el manejo de caracteres especiales en el transporte HTTP.

### 📍 Fase 2: Persistencia Offline (Medio Plazo)
*   **Lector MicroSD:** Integración de hardware para almacenamiento local mediante bus SPI.
*   **Sincronización Inteligente:** 
    *   Guardado automático de registros en la MicroSD cuando falla la conexión WiFi/Internet.
    *   Lógica de "Checkpoint": Al recuperar la conexión, el dispositivo verificará cuál fue el último registro recibido por el servidor y enviará en ráfaga los datos pendientes para garantizar **cero pérdida de información**.

### 📍 Fase 3: Modernización de Hardware (Investigación)
*   **Soporte para Apple/Google Wallet:** Investigación y prototipado con lectores que soporten protocolos **NFC VAS (Apple)** y **Smart Tap (Google)**. El objetivo es permitir que los empleados usen sus pases digitales en el móvil en lugar de tarjetas físicas, mejorando la seguridad y la experiencia de usuario.

---

## 📄 Licencia
MIT License.




