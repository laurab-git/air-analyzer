#!/bin/bash
# Script pour upload OTA manuel
# Usage: ./upload_ota.sh

ESP_IP="192.168.68.51"
ESP_PORT="3232"
OTA_PASSWORD="Baudoin98"

# Trouver le fichier .bin compilé
BIN_FILE=$(find /tmp/arduino* -name "air_analyzer.ino.bin" 2>/dev/null | head -1)

if [ -z "$BIN_FILE" ]; then
    echo "❌ Fichier .bin non trouvé. Compilez d'abord dans Arduino IDE."
    exit 1
fi

echo "📡 Upload OTA vers $ESP_IP"
echo "📦 Fichier: $BIN_FILE"

# Trouver espota.py
ESPOTA=$(find ~/.arduino15/packages/esp32 -name "espota.py" 2>/dev/null | head -1)

if [ -z "$ESPOTA" ]; then
    echo "❌ espota.py non trouvé"
    exit 1
fi

# Upload
python3 "$ESPOTA" -i "$ESP_IP" -p "$ESP_PORT" -a "$OTA_PASSWORD" -f "$BIN_FILE"

echo "✅ Upload terminé !"
