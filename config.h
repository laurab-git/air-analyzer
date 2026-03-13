#ifndef CONFIG_H
#define CONFIG_H

// ============================================================
// --- CÂBLAGE MATÉRIEL ---
// ============================================================
// Écran TFT SPI (ST7789 1.9")
// VCC  -> 3.3V  | GND  -> GND
// CS   -> GPIO 0 | RES  -> GPIO 3 | DC   -> GPIO 2
// BLK  -> GPIO 1 | SDA  -> GPIO 7 | SCL  -> GPIO 4
#define TFT_CS 0
#define TFT_DC 2
#define TFT_RST 3
#define TFT_BL 1
#define TFT_MOSI 7
#define TFT_SCLK 4

// Capteur CO2 SCD40 (I2C)
// VIN  -> 3.3V | GND -> GND | SDA -> GPIO 5 | SCL -> GPIO 6
#define I2C_SDA 5
#define I2C_SCL 6

// Bouton tactile (GPIO avec pull-up interne)
// Connecter bouton entre GPIO 9 et GND (GPIO 8 peut avoir des conflits sur ESP32-C3)
#define BUTTON_PIN 9

// ============================================================
// --- CONSTANTES TEMPORELLES ---
// ============================================================
#define SENSOR_INTERVAL_MS 300000UL   // 5 minutes
#define DISPLAY_INTERVAL_MS 250UL     // Rafraîchissement affichage
#define WEATHER_INTERVAL_MS 3600000UL // 1 heure
#define MQTT_RETRY_MS 5000UL          // Retry MQTT
#define WIFI_RETRY_MS 10000UL         // Retry WiFi
#define VIEW_CYCLE_MS 5000UL          // Durée de chaque vue du cycle
#define WDT_TIMEOUT_S 60              // Watchdog : reset après 60s (permet l'OTA)

// Bouton
#define DEBOUNCE_MS 50UL              // Antirebond
#define LONG_PRESS_MS 1000UL          // Durée appui long (1 seconde)
#define DOUBLE_PRESS_WINDOW_MS 400UL  // Fenêtre détection double appui

// ============================================================
// --- CALIBRATION CAPTEUR ---
// ============================================================
#define TEMP_OFFSET 1.7f              // Correction offset thermique SCD40 (°C)
// Calibré : SCD40 affichait 23.8°C pour 22.1°C réel

// ============================================================
// --- TIMEZONE ET NTP ---
// ============================================================
// Timezone POSIX Europe/Paris : gère automatiquement heure été/hiver
#define TIMEZONE_STR "CET-1CEST,M3.5.0,M10.5.0/3"
#define NTP_SERVER "pool.ntp.org"

#endif // CONFIG_H
