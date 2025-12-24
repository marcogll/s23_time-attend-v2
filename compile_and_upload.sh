#!/bin/bash
# Script para compilar, cargar y abrir monitor serial del checador NFC
# Carga con barra de progreso REAL (parseando esptool)

# ───────────────────── Colores ─────────────────────
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'
BOLD='\033[1m'

# ─────────────────── Configuración ──────────────────
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SKETCH_DIR="$SCRIPT_DIR"
SKETCH_NAME="soul23_time_attendance"
FQBN="esp8266:esp8266:nodemcuv2"
PORT="/dev/ttyUSB0"
BAUD_RATE="115200"

# ───────────────── Funciones ────────────────────────
show_progress() {
    local current=$1
    local total=$2
    local width=50
    local percent=$((current * 100 / total))
    local filled=$((current * width / total))
    local empty=$((width - filled))

    printf "\r${CYAN}["
    printf "%${filled}s" | tr ' ' '█'
    printf "%${empty}s" | tr ' ' '░'
    printf "] %3d%%%s${NC}" "$percent" " "
}

check_device_status() {
    local port=$1

    if [ -e "$port" ]; then
        if arduino-cli board list 2>/dev/null | grep -q "$port"; then
            echo -e "${GREEN}✅ Conectado${NC}"
        else
            echo -e "${YELLOW}⚠️  Puerto existe (no identificado)${NC}"
        fi
    else
        echo -e "${RED}❌ No conectado${NC}"
    fi
}

detect_port() {
    arduino-cli board list | grep -E "ttyUSB|ttyACM" | head -1 | awk '{print $1}'
}

# ───────────────── Encabezado ───────────────────────
clear
echo -e "${BOLD}${BLUE}╔══════════════════════════════════════════════════════════╗${NC}"
echo -e "${BOLD}${BLUE}║     Checador NFC - Compilación y Carga Automática        ║${NC}"
echo -e "${BOLD}${BLUE}╚══════════════════════════════════════════════════════════╝${NC}\n"

# ───────────────── Paso 1 ───────────────────────────
echo -e "${BOLD}${CYAN}[1/4]${NC} Verificando conexión del dispositivo..."
echo -e "   Puerto configurado: ${BOLD}$PORT${NC}"
echo -e "   Estado: $(check_device_status "$PORT")"

if [ ! -e "$PORT" ]; then
    auto_port=$(detect_port)
    if [ -z "$auto_port" ]; then
        echo -e "${RED}❌ No se detectó ningún dispositivo${NC}"
        exit 1
    fi
    PORT="$auto_port"
    echo -e "   Puerto detectado automáticamente: ${BOLD}$PORT${NC}"
fi

echo

# ───────────────── Paso 2 ───────────────────────────
echo -e "${BOLD}${CYAN}[2/4]${NC} Compilando sketch..."
arduino-cli compile --fqbn "$FQBN" "$SKETCH_DIR/$SKETCH_NAME.ino"
if [ $? -ne 0 ]; then
    echo -e "${RED}❌ Error de compilación${NC}"
    exit 1
fi
echo -e "${GREEN}✅ Compilación exitosa${NC}\n"

# ───────────────── Paso 3 (PROGRESO REAL) ───────────
echo -e "${BOLD}${CYAN}[3/4]${NC} Cargando firmware al dispositivo..."
echo -e "   Puerto: ${BOLD}$PORT${NC}"
echo -e "   Estado: $(check_device_status "$PORT")\n"

upload_failed=0
last_percent=0

arduino-cli upload \
    -p "$PORT" \
    --fqbn "$FQBN" \
    "$SKETCH_DIR/$SKETCH_NAME.ino" \
    --verbose 2>&1 | \
while IFS= read -r line; do
    if [[ "$line" =~ \(([0-9]+)\ \%\) ]]; then
        percent="${BASH_REMATCH[1]}"
        if [ "$percent" -ge "$last_percent" ]; then
            show_progress "$percent" 100
            last_percent="$percent"
        fi
    elif echo "$line" | grep -qi "Hash of data verified"; then
        show_progress 100 100
        printf "\n"
    elif echo "$line" | grep -qi "error\|failed\|timeout"; then
        upload_failed=1
        echo -e "\n${RED}❌ Error durante la carga${NC}"
        echo "$line"
    fi
done

if [ "$upload_failed" -ne 0 ]; then
    exit 1
fi

echo -e "${GREEN}✅ Firmware cargado exitosamente${NC}\n"

# ───────────────── Paso 4 ───────────────────────────
echo -e "${BOLD}${CYAN}[4/4]${NC} Abriendo monitor serial..."
echo -e "   Puerto: ${BOLD}$PORT${NC}"
echo -e "   Baud rate: ${BOLD}$BAUD_RATE${NC}\n"

sleep 1
arduino-cli monitor -p "$PORT" --config baudrate="$BAUD_RATE"

