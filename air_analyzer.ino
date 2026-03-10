#include "secrets.h"
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include <SPI.h>
#include <SensirionI2cScd4x.h>
#include <WiFi.h>
#include <esp_task_wdt.h>
#include <string.h>

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

// ============================================================
// --- CONSTANTES ---
// ============================================================
#define SENSOR_INTERVAL_MS 300000UL   // 5 minutes
#define DISPLAY_INTERVAL_MS 250UL     // Rafraîchissement affichage
#define WEATHER_INTERVAL_MS 3600000UL // 1 heure
#define MQTT_RETRY_MS 5000UL          // Retry MQTT
#define WIFI_RETRY_MS 10000UL         // Retry WiFi
#define BOOT_DURATION_MS 60000UL      // Phase de boot : 1 minute
#define VIEW_CYCLE_MS 5000UL          // Durée de chaque vue du cycle
#define WDT_TIMEOUT_S 30              // Watchdog : reset après 30s de blocage
#define TEMP_OFFSET 0.0f              // Correction offset thermique SCD40 (°C)
// À calibrer empiriquement selon votre montage

// Timezone POSIX Europe/Paris : gère automatiquement heure été/hiver
#define TIMEZONE_STR "CET-1CEST,M3.5.0,M10.5.0/3"
#define NTP_SERVER "pool.ntp.org"

// ============================================================
// --- OBJETS MATÉRIELS ---
// ============================================================
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
SensirionI2cScd4x scd4x;
WiFiClient espClient;
PubSubClient mqttClient(espClient);

// ============================================================
// --- DONNÉES RTC (Persistantes après reboot/deep-sleep) ---
// ============================================================
// OPT#10 : RTC_DATA_ATTR — ces variables survivent aux resets logiciels.
// Analogie : c'est le post-it collé sur le frigo, pas le tableau blanc.
RTC_DATA_ATTR uint16_t nightMaxCO2 = 0;
RTC_DATA_ATTR float nightMinTemp = 100.0f;
RTC_DATA_ATTR float nightMaxHum = 0.0f;
RTC_DATA_ATTR bool nightDataAvail = false;

RTC_DATA_ATTR uint16_t dayMaxCO2 = 0;
RTC_DATA_ATTR float dayMinTemp = 100.0f;
RTC_DATA_ATTR float dayMaxHum = 0.0f;
RTC_DATA_ATTR bool dayDataAvail = false;

RTC_DATA_ATTR int lastResetHour =
    -1; // Aussi persistant pour éviter double-reset

// ============================================================
// --- ÉTAT GLOBAL ---
// ============================================================
bool isDisplayOn = true;
bool bootPhaseComplete = false;
unsigned long bootTimeStart = 0;

// OPT#1 : Une seule variable lastCO2, globale, sans doublon dans displayUI()
uint16_t lastCO2 = 0;
float lastTemp = -100.0f;
float lastHum = -100.0f;
bool hasValidData = false;

// Pour le redraw conditionnel de l'affichage
char currentScreenLabel[32] = "";
int lastDisplayMinute = -1;

// Météo
uint8_t weatherCode = 0;
float weatherMaxTemp = 0.0f;
float weatherMinTemp = 0.0f;
bool hasWeatherData = false;
bool isWeatherTomorrow = false;

// Snapshot de l'heure locale, récupéré UNE SEULE FOIS par loop()
// OPT#7 : évite de rappeler getLocalTime() dans chaque sous-fonction
struct tm g_timeinfo;
bool g_timeValid = false;

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);

  // OPT#11 : Watchdog matériel — si loop() se bloque plus de 30s, reboot auto
  const esp_task_wdt_config_t wdt_config = {
      .timeout_ms = WDT_TIMEOUT_S * 1000,
      .idle_core_mask = 0,
      .trigger_panic = true,
  };
  esp_task_wdt_init(&wdt_config);
  esp_task_wdt_add(NULL);

  // Rétroéclairage PWM
  ledcAttach(TFT_BL, 5000, 8);
  ledcWrite(TFT_BL, 120);

  // Écran
  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
  tft.init(170, 320);
  tft.setRotation(1);
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_WHITE);

  // --- Connexion WiFi ---
  tft.setCursor(10, 40);
  tft.println("Connexion WiFi...");
  tft.setTextSize(1);
  tft.setCursor(10, 70);
  tft.print("SSID: ");
  tft.println(WIFI_SSID);

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 15000) {
    delay(500);
    tft.print(".");
    esp_task_wdt_reset(); // Nourrir le watchdog pendant l'attente bloquante du
                          // boot
  }

  tft.fillScreen(ST77XX_BLACK);
  tft.setCursor(10, 60);
  tft.setTextSize(2);

  if (WiFi.status() == WL_CONNECTED) {
    tft.setTextColor(ST77XX_GREEN);
    tft.println("WIFI OK !");
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_WHITE);
    tft.print("IP: ");
    tft.println(WiFi.localIP());

    // OPT#3 : Timezone POSIX — heure été/hiver gérée automatiquement
    configTzTime(TIMEZONE_STR, NTP_SERVER);

    mqttClient.setServer(MQTT_SERVER, 1883);
  } else {
    tft.setTextColor(ST77XX_RED);
    tft.println("WIFI ERREUR");
    tft.setTextSize(1);
    tft.println("Mode hors-ligne...");
  }

  delay(3000);
  esp_task_wdt_reset();

  // --- Capteur SCD40 ---
  Wire.begin(I2C_SDA, I2C_SCL);
  scd4x.begin(Wire, 0x62);
  scd4x.stopPeriodicMeasurement();
  delay(500);

  // OPT#9 : Low Power Mode — mesure toutes les 30s au lieu de 5s.
  // On lit toutes les 5 minutes, donc 30s est largement suffisant.
  // Avantage : réduction de l'auto-échauffement → température plus précise.
  scd4x.startLowPowerPeriodicMeasurement();

  // OPT#8 : Offset de température pour compenser la chaleur de l'ESP32
  scd4x.setTemperatureOffset(TEMP_OFFSET);

  bootTimeStart = millis();
  tft.fillScreen(ST77XX_BLACK);

  esp_task_wdt_reset();
}

// ============================================================
// LOOP
// ============================================================
void loop() {
  unsigned long currentMillis = millis();

  // OPT#11 : Nourrir le watchdog — preuve que le programme n'est pas bloqué
  esp_task_wdt_reset();

  // OPT#7 : Lecture de l'heure UNE SEULE FOIS par cycle de loop()
  // Toutes les fonctions utilisent g_timeinfo / g_timeValid
  g_timeValid = getLocalTime(&g_timeinfo);

  // ── 0. RECONNEXION WIFI ──────────────────────────────────────────────────
  if (WiFi.status() != WL_CONNECTED) {
    static unsigned long lastWiFiReconnect = 0;
    // OPT#4 : Arithmétique unsigned — robuste à l'overflow de millis() (~49j)
    if ((unsigned long)(currentMillis - lastWiFiReconnect) >= WIFI_RETRY_MS) {
      lastWiFiReconnect = currentMillis;
      Serial.println("WiFi perdu... Tentative de reconnexion.");
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASS);
    }
  }

  // ── 1. LECTURE CAPTEUR (toutes les 5 minutes) ───────────────────────────
  static unsigned long lastSensorReadTime = 0;
  static bool sensorBootWait = true;

  // OPT#2 : Au boot, on attend que le capteur soit prêt plutôt qu'une lecture
  // immédiate ratée. On retry toutes les 5s pendant la phase de boot.
  unsigned long sensorCheckInterval =
      sensorBootWait ? 5000UL : SENSOR_INTERVAL_MS;

  if ((unsigned long)(currentMillis - lastSensorReadTime) >=
      sensorCheckInterval) {
    bool ready = false;
    scd4x.getDataReadyStatus(ready);

    if (ready) {
      uint16_t co2;
      float temp, hum;
      if (scd4x.readMeasurement(co2, temp, hum) == 0) {
        lastSensorReadTime = currentMillis;
        sensorBootWait =
            false; // Première mesure obtenue : on passe en rythme normal

        lastCO2 = co2;
        lastTemp = temp;
        lastHum = hum;
        hasValidData = true;

        updateStats(co2, temp, hum);

        if (mqttClient.connected()) {
          char payload[128];
          snprintf(payload, sizeof(payload),
                   "{\"temperature\":%.2f,\"humidity\":%.2f,\"co2\":%d}", temp,
                   hum, co2);
          mqttClient.publish("weather_probe/sensor/state", payload);
          Serial.printf("MQTT publié: %s\n", payload);
        }
      }
    } else if (!sensorBootWait) {
      // En rythme normal, si le capteur n'est pas prêt on reporte de 5s
      lastSensorReadTime = currentMillis - SENSOR_INTERVAL_MS + 5000UL;
    }
    // En phase boot (sensorBootWait), on retente simplement à la prochaine
    // itération de 5s
  }

  // ── 2. AFFICHAGE (toutes les 250ms — OPT#6) ─────────────────────────────
  static unsigned long lastDisplayUpdate = 0;
  if ((unsigned long)(currentMillis - lastDisplayUpdate) >=
      DISPLAY_INTERVAL_MS) {
    lastDisplayUpdate = currentMillis;
    updateDisplayCycle();
  }

  // ── 3. MÉTÉO (toutes les heures) ────────────────────────────────────────
  static unsigned long lastWeatherFetch = 0;
  if (WiFi.status() == WL_CONNECTED &&
      (lastWeatherFetch == 0 ||
       (unsigned long)(currentMillis - lastWeatherFetch) >=
           WEATHER_INTERVAL_MS)) {
    lastWeatherFetch = currentMillis;
    fetchWeather();
  }

  // ── 4. MQTT (reconnexion non-bloquante) ─────────────────────────────────
  if (WiFi.status() == WL_CONNECTED) {
    if (!mqttClient.connected()) {
      static unsigned long lastMqttReconnect = 0;
      if ((unsigned long)(currentMillis - lastMqttReconnect) >= MQTT_RETRY_MS) {
        lastMqttReconnect = currentMillis;

        String clientId = "WeatherProbe-" + String(WiFi.macAddress());
        Serial.printf("Tentative MQTT (%s)... ", clientId.c_str());

        if (mqttClient.connect(clientId.c_str(), MQTT_USER, MQTT_PASS)) {
          Serial.println("connecté !");
        } else {
          int state = mqttClient.state();
          Serial.printf("échec rc=%d%s\n", state,
                        state == 5   ? " (Identifiants incorrects)"
                        : state == 2 ? " (Client ID rejeté)"
                                     : "");
        }
      }
    } else {
      mqttClient.loop();
    }
  }

  // ── 5. CURSEUR CLIGNOTANT (toutes les 500ms) ────────────────────────────
  static unsigned long lastCursorToggle = 0;
  static bool cursorVisible = true;
  if ((unsigned long)(currentMillis - lastCursorToggle) >= 500UL) {
    lastCursorToggle = currentMillis;
    cursorVisible = !cursorVisible;
    tft.setCursor(5, 160);
    tft.setTextSize(1);
    tft.setTextColor(cursorVisible ? ST77XX_WHITE : ST77XX_BLACK);
    tft.print("_");
  }

  // Pas de delay() : la loop tourne librement, chaque tâche gère son timer
}

// ============================================================
// updateStats — Mise à jour des extrêmes nuit/jour
// ============================================================
void updateStats(uint16_t co2, float t, float h) {
  // OPT#7 : Utilisation de g_timeinfo global, pas de nouvel appel système
  int currentHour = g_timeValid ? g_timeinfo.tm_hour : 12;
  bool isNight = (currentHour >= 22 || currentHour < 7);

  if (isNight) {
    nightDataAvail = true;
    if (co2 > nightMaxCO2)
      nightMaxCO2 = co2;
    if (t < nightMinTemp)
      nightMinTemp = t;
    if (h > nightMaxHum)
      nightMaxHum = h;
  } else {
    dayDataAvail = true;
    if (co2 > dayMaxCO2)
      dayMaxCO2 = co2;
    if (t < dayMinTemp)
      dayMinTemp = t;
    if (h > dayMaxHum)
      dayMaxHum = h;
  }

  // Reset des stats aux heures charnières (détection de changement d'heure)
  // OPT#10 : lastResetHour est RTC_DATA_ATTR → pas de double-reset après reboot
  if (g_timeValid && lastResetHour != currentHour) {
    if (currentHour == 22) {
      nightMaxCO2 = co2;
      nightMinTemp = t;
      nightMaxHum = h;
      nightDataAvail = true; // La mesure courante devient le premier point
    }
    if (currentHour == 7) {
      dayMaxCO2 = co2;
      dayMinTemp = t;
      dayMaxHum = h;
      dayDataAvail = true;
    }
    lastResetHour = currentHour;
  }
}

// ============================================================
// updateBacklight
// ============================================================
void updateBacklight(bool on) {
  isDisplayOn = on;
  ledcWrite(TFT_BL, on ? 80 : 0);
}

// ============================================================
// updateDisplayCycle — Sélection du mode d'affichage
// ============================================================
void updateDisplayCycle() {
  // OPT#7 : g_timeinfo déjà rempli en haut de loop()
  int currentHour = g_timeValid ? g_timeinfo.tm_hour : 12;
  unsigned long currentMillis = millis();

  // Phase de boot (1 minute)
  if (!bootPhaseComplete) {
    displayUI(lastCO2, lastTemp, lastHum,
              hasValidData ? "INITIALISATION (1mn)" : "ATTENTE CAPTEUR...");
    if ((unsigned long)(currentMillis - bootTimeStart) > BOOT_DURATION_MS)
      bootPhaseComplete = true;
    return;
  }

  if (!hasValidData) {
    displayUI(0, 0, 0, "ATTENTE CAPTEUR...");
    return;
  }

  bool isNight = (currentHour >= 22 || currentHour < 7);
  if (isNight) {
    if (isDisplayOn) {
      updateBacklight(false);
      tft.fillScreen(ST77XX_BLACK);
    }
    return;
  }

  if (!isDisplayOn)
    updateBacklight(true);

  // Cycle des vues (valeurs actuelles: 10s, autres: 5s)
  int modes[4];
  int modeCount = 0;
  modes[modeCount++] = 0; // Valeurs actuelles — toujours présent
  if (hasWeatherData)
    modes[modeCount++] = 3;
  if (nightDataAvail && nightMaxCO2 > 0)
    modes[modeCount++] = 1;
  if (dayDataAvail && dayMaxCO2 > 0)
    modes[modeCount++] = 2;

  // OPT#5 : On fige l'index au changement de modeCount pour éviter les sauts
  static int lastModeCount = 0;
  static int currentViewIdx = 0;
  static unsigned long lastViewSwitch = 0;

  if (modeCount != lastModeCount) {
    // Le cycle a changé (ex: météo vient d'arriver) → on repart à 0
    currentViewIdx = 0;
    lastViewSwitch = currentMillis;
    lastModeCount = modeCount;
  } else {
    // Durée d'affichage : 10s pour les valeurs actuelles, 5s pour les autres
    unsigned long viewDuration = (modes[currentViewIdx] == 0) ? 10000UL : VIEW_CYCLE_MS;
    if ((unsigned long)(currentMillis - lastViewSwitch) >= viewDuration) {
      currentViewIdx = (currentViewIdx + 1) % modeCount;
      lastViewSwitch = currentMillis;
    }
  }

  switch (modes[currentViewIdx]) {
  case 0:
    displayUI(lastCO2, lastTemp, lastHum, "VALEURS ACTUELLES");
    break;
  case 1:
    displayUI(nightMaxCO2, nightMinTemp, nightMaxHum, "EXTREMES DE LA NUIT");
    break;
  case 2:
    displayUI(dayMaxCO2, dayMinTemp, dayMaxHum, "EXTREMES DU JOUR");
    break;
  case 3:
    displayWeatherUI(weatherCode, weatherMaxTemp, weatherMinTemp,
                     isWeatherTomorrow);
    break;
  }
}

// ============================================================
// displayUI — Affichage principal CO2/Temp/Hum
// ============================================================
void displayUI(uint16_t co2, float t, float h, const char *label) {
  // OPT#1 : Variables statiques locales uniquement — plus de doublon avec
  // global
  static uint16_t prevCO2 = 0xFFFF;
  static float prevT = -100.0f;
  static float prevH = -100.0f;
  static uint16_t prevColor = 0;

  bool labelChanged = (strcmp(label, currentScreenLabel) != 0);

  if (labelChanged) {
    tft.fillScreen(ST77XX_BLACK);
    tft.setCursor(10, 10);
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_WHITE);
    tft.println(label);
    tft.drawFastHLine(10, 115, 300, 0x7BEF);

    tft.setTextSize(2);
    tft.setCursor(215, 90);
    tft.setTextColor(ST77XX_WHITE);
    if (strcmp(label, "VALEURS ACTUELLES") == 0 ||
        strstr(label, "EXTREMES") != NULL)
      tft.print("ppm");

    strncpy(currentScreenLabel, label, sizeof(currentScreenLabel));
    lastDisplayMinute = -1; // Forcer le redessin de l'heure

    // Réinitialiser les caches pour forcer le redraw complet
    prevCO2 = 0xFFFF;
    prevT = -100.0f;
    prevH = -100.0f;
  }

  // Heure (OPT#7 : depuis g_timeinfo)
  if (g_timeValid && (g_timeinfo.tm_min != lastDisplayMinute || labelChanged)) {
    tft.fillRect(250, 10, 60, 20, ST77XX_BLACK);
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(260, 10);
    tft.printf("%02d:%02d", g_timeinfo.tm_hour, g_timeinfo.tm_min);
    lastDisplayMinute = g_timeinfo.tm_min;
  }

  // Couleur et niveau AQI
  uint16_t color = ST77XX_GREEN;
  int aqiLevel = 0;
  if (co2 > 1500) {
    color = ST77XX_RED;
    aqiLevel = 3;
  } else if (co2 > 1000) {
    color = ST77XX_ORANGE;
    aqiLevel = 2;
  } else if (co2 > 800) {
    color = ST77XX_YELLOW;
    aqiLevel = 1;
  }

  // Jauge AQI
  static int lastAqiLevel = -1;
  if (strcmp(label, "VALEURS ACTUELLES") == 0 &&
      (labelChanged || aqiLevel != lastAqiLevel)) {
    int gaugeX = 280, gaugeY = 40, gaugeW = 15, gaugeH = 60;
    tft.fillRect(gaugeX, gaugeY, gaugeW, gaugeH, 0x4A69);
    tft.drawRect(gaugeX - 1, gaugeY - 1, gaugeW + 2, gaugeH + 2, ST77XX_WHITE);
    int fillH = (aqiLevel + 1) * (gaugeH / 4);
    tft.fillRect(gaugeX, gaugeY + (gaugeH - fillH), gaugeW, fillH, color);
    lastAqiLevel = aqiLevel;
  }

  // CO2
  if (labelChanged || co2 != prevCO2 || color != prevColor) {
    tft.setCursor(45, 55);
    tft.setTextSize(6);
    tft.setTextColor(color, ST77XX_BLACK);
    tft.printf("%4u", co2);
    prevCO2 = co2;
    prevColor = color;
  }

  // Température
  if (labelChanged || fabsf(t - prevT) >= 0.1f) {
    tft.setTextSize(2);
    tft.setCursor(20, 135);
    tft.setTextColor(ST77XX_CYAN, ST77XX_BLACK);
    tft.printf("%4.1f C", t);
    prevT = t;
  }

  // Humidité
  if (labelChanged || fabsf(h - prevH) >= 1.0f) {
    tft.setCursor(190, 135);
    tft.setTextColor(ST77XX_MAGENTA, ST77XX_BLACK);
    tft.printf("%3.0f%% HR", h);
    prevH = h;
  }
}

// ============================================================
// fetchWeather — Récupération météo Open-Meteo
// ============================================================
void fetchWeather() {
  if (WiFi.status() != WL_CONNECTED)
    return;
  Serial.println("Récupération météo...");

  WiFiClient weatherClient;
  HTTPClient http;

  String url =
      "http://api.open-meteo.com/v1/forecast?latitude=" + String(LATITUDE, 4) +
      "&longitude=" + String(LONGITUDE, 4) +
      "&daily=weather_code,temperature_2m_max,temperature_2m_min" +
      "&timezone=Europe%2FParis";

  http.begin(weatherClient, url);
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, http.getStream());

    if (!err) {
      // OPT#7 : g_timeinfo global
      int currentHour = g_timeValid ? g_timeinfo.tm_hour : 12;
      int dayIndex = (currentHour >= 17) ? 1 : 0;

      weatherCode = doc["daily"]["weather_code"][dayIndex];
      weatherMaxTemp = doc["daily"]["temperature_2m_max"][dayIndex];
      weatherMinTemp = doc["daily"]["temperature_2m_min"][dayIndex];
      isWeatherTomorrow = (dayIndex == 1);
      hasWeatherData = true;

      Serial.printf("Météo: Code %d, Max %.1f, Min %.1f\n", weatherCode,
                    weatherMaxTemp, weatherMinTemp);
    } else {
      Serial.printf("Erreur JSON: %s\n", err.c_str());
    }
  } else {
    Serial.printf("Erreur HTTP météo: %d\n", httpCode);
  }
  http.end();
}

// ============================================================
// drawWeatherIcon — Icône météo graphique
// ============================================================
void drawWeatherIcon(uint8_t code, int x, int y) {
  const uint16_t C_SUN = 0xFFE0;       // Jaune
  const uint16_t C_CLOUD = 0xCE59;     // Gris clair
  const uint16_t C_DARKCLOUD = 0x4208; // Gris foncé
  const uint16_t C_RAIN = 0x001F;      // Bleu
  const uint16_t C_SNOW = 0xFFFF;      // Blanc

  tft.fillRect(x - 20, y - 15, 40, 45, ST77XX_BLACK);

  if (code <= 1) {
    tft.fillCircle(x, y, 14, C_SUN);
  } else if (code == 2) {
    tft.fillCircle(x - 8, y - 8, 12, C_SUN);
    tft.fillCircle(x, y + 5, 10, C_CLOUD);
    tft.fillCircle(x + 12, y + 5, 12, C_CLOUD);
    tft.fillCircle(x + 5, y - 4, 14, C_CLOUD);
  } else if (code == 3 || code == 45 || code == 48) {
    tft.fillCircle(x - 12, y + 5, 10, C_CLOUD);
    tft.fillCircle(x + 12, y + 5, 12, C_CLOUD);
    tft.fillCircle(x, y - 4, 14, C_CLOUD);
    tft.fillCircle(x, y + 5, 12, C_CLOUD);
  } else if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) {
    tft.fillCircle(x - 12, y, 10, C_DARKCLOUD);
    tft.fillCircle(x + 12, y, 12, C_DARKCLOUD);
    tft.fillCircle(x, y - 9, 14, C_DARKCLOUD);
    tft.drawLine(x - 10, y + 15, x - 14, y + 25, C_RAIN);
    tft.drawLine(x, y + 15, x - 4, y + 25, C_RAIN);
    tft.drawLine(x + 10, y + 15, x + 6, y + 25, C_RAIN);
  } else if ((code >= 71 && code <= 77) || code == 85 || code == 86) {
    tft.fillCircle(x - 12, y, 10, C_CLOUD);
    tft.fillCircle(x + 12, y, 12, C_CLOUD);
    tft.fillCircle(x, y - 9, 14, C_CLOUD);
    tft.fillCircle(x - 10, y + 20, 2, C_SNOW);
    tft.fillCircle(x, y + 24, 2, C_SNOW);
    tft.fillCircle(x + 10, y + 20, 2, C_SNOW);
  } else if (code >= 95) {
    tft.fillCircle(x - 12, y, 10, C_DARKCLOUD);
    tft.fillCircle(x + 12, y, 12, C_DARKCLOUD);
    tft.fillCircle(x, y - 9, 14, C_DARKCLOUD);
    tft.drawLine(x, y + 10, x - 6, y + 20, C_SUN);
    tft.drawLine(x - 6, y + 20, x + 4, y + 20, C_SUN);
    tft.drawLine(x + 4, y + 20, x - 2, y + 30, C_SUN);
  }
}

// ============================================================
// displayWeatherUI — Écran météo
// ============================================================
void displayWeatherUI(uint8_t code, float maxT, float minT, bool tomorrow) {
  const char *label = tomorrow ? "METEO DEMAIN" : "METEO AUJOURD'HUI";
  bool labelChanged = (strcmp(label, currentScreenLabel) != 0);

  if (labelChanged) {
    tft.fillScreen(ST77XX_BLACK);
    tft.setCursor(10, 10);
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_WHITE);
    tft.println(label);
    tft.drawFastHLine(10, 115, 300, 0x7BEF);

    tft.setTextSize(2);
    tft.setCursor(150, 45);
    tft.setTextColor(ST77XX_ORANGE);
    tft.print("Max");
    tft.setCursor(150, 85);
    tft.setTextColor(ST77XX_CYAN);
    tft.print("Min");

    drawWeatherIcon(code, 60, 65);

    tft.setTextSize(3);
    tft.setTextColor(ST77XX_ORANGE, ST77XX_BLACK);
    tft.setCursor(200, 40);
    tft.printf("%4.1f", maxT);
    tft.setTextColor(ST77XX_CYAN, ST77XX_BLACK);
    tft.setCursor(200, 80);
    tft.printf("%4.1f", minT);

    strncpy(currentScreenLabel, label, sizeof(currentScreenLabel));
    lastDisplayMinute = -1;
  }

  // Heure (OPT#7 : depuis g_timeinfo)
  if (g_timeValid && (g_timeinfo.tm_min != lastDisplayMinute || labelChanged)) {
    tft.fillRect(250, 10, 60, 20, ST77XX_BLACK);
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(260, 10);
    tft.printf("%02d:%02d", g_timeinfo.tm_hour, g_timeinfo.tm_min);
    lastDisplayMinute = g_timeinfo.tm_min;
  }
}
