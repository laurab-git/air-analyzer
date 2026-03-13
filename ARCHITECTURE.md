# Architecture technique - Air Analyzer

Documentation détaillée de l'architecture du firmware pour développeurs et agents IA.

## Vue d'ensemble du système

```
┌─────────────────────────────────────────────────────────────┐
│                    ESP32-C3 SuperMini                        │
│                                                              │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐      │
│  │   Capteur    │  │  Affichage   │  │   Bouton     │      │
│  │   SCD40      │  │   ST7789     │  │  GPIO 9      │      │
│  │   (I2C)      │  │   (SPI)      │  │              │      │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘      │
│         │                 │                 │               │
│         ├─────────────────┼─────────────────┤               │
│         │                 │                 │               │
│  ┌──────▼─────────────────▼─────────────────▼──────┐       │
│  │           Core Firmware (loop)                   │       │
│  │  • Watchdog (60s)                                │       │
│  │  • Time sync (NTP)                               │       │
│  │  • Event handlers                                │       │
│  └──────┬───────────────────────────────────────────┘       │
│         │                                                    │
│  ┌──────▼──────────────────────────────────────────┐        │
│  │         Network Stack (WiFi)                     │        │
│  │  • MQTT Client (Home Assistant)                 │        │
│  │  • HTTP Client (Météo API)                      │        │
│  │  • OTA Handler (mDNS: air-analyzer.local)       │        │
│  └──────────────────────────────────────────────────┘        │
└──────────────────────────────────────────────────────────────┘
```

## Diagramme de flux de données

```
┌──────────────┐
│  Setup()     │
└──────┬───────┘
       │
       ├─> initWatchdog()
       ├─> initButton()
       ├─> initDisplay() ──> Écran de bienvenue (2s)
       ├─> initNetwork() ──> WiFi + MQTT + OTA + NTP
       └─> initSensor() ──> Calibration SCD40

┌──────────────┐
│   Loop()     │  (exécution continue, ~250ms par cycle)
└──────┬───────┘
       │
       ├─> [OTA in progress?] ──YES──> handleOTA() only ──> return
       │                         NO
       ├─> feedWatchdog() ───────────> Reset timer 60s
       ├─> updateTime() ─────────────> Sync NTP (1x/jour)
       │
       ├─> handleWiFiReconnect() ────> Check & reconnect WiFi
       │
       ├─> handleButton() ───────────> Détecte événements bouton
       │     │
       │     ├─ SHORT_PRESS ──────────> nextDisplayView()
       │     ├─ LONG_PRESS ───────────> toggleDisplayPower() + publishDisplayState()
       │     └─ DOUBLE_PRESS ─────────> setDisplayMode(MANUAL) + cycle brightness
       │
       ├─> handleSensorRead() ───────> Tous les 5 min
       │     │
       │     ├─> Lecture SCD40 (CO2, temp, hum)
       │     ├─> updateStats() ──────> Min/Max jour/nuit
       │     └─> publishSensorData()
       │
       ├─> handleDisplayUpdate() ────> Tous les 250ms
       │     │
       │     └─> updateDisplayCycle()
       │           │
       │           ├─ Mode OFF ──────> Éteindre écran
       │           ├─ Mode AUTO ─────> getAutoBrightness() selon heure
       │           ├─ Mode MANUAL ───> Utiliser manualBrightness/Power
       │           │
       │           └─> Cycle de vues (4 vues possibles)
       │                 ├─ Vue 0: Valeurs actuelles (10s)
       │                 ├─ Vue 1: Extrêmes nuit (5s)
       │                 ├─ Vue 2: Extrêmes jour (5s)
       │                 └─ Vue 3: Météo (5s)
       │
       ├─> handleWeatherFetch() ─────> Toutes les heures
       │     │
       │     └─> HTTP GET Open-Meteo API
       │
       ├─> handleOTA() ──────────────> ArduinoOTA.handle()
       │
       └─> handleMQTT() ─────────────>
             │
             ├─ [Disconnected?] ─────> Reconnect + subscribe topics
             ├─ mqttCallback() ──────> Traite commandes reçues
             │     ├─ display/mode/set
             │     ├─ display/brightness/set
             │     └─ display/power/set
             │
             └─ mqttClient.loop() ───> Traitement messages

```

## Architecture des modules

### Module Button (button.cpp/h)

**Responsabilité**: Détection robuste des événements bouton

**Machine à états**:
```
IDLE ──[press]──> PRESSED ──[hold 1s]──> LONG_PRESS
  ▲                  │
  │                  └──[release < 1s]──> RELEASED
  │                                         │
  │                                         ├─[timeout]──> SHORT_PRESS ──> IDLE
  │                                         └─[press < 400ms]──> DOUBLE_PRESS ──> IDLE
  │
  └──────────────────────────────────────────────────────────────────────────────┘
```

**Variables d'état**:
```cpp
static unsigned long lastDebounceTime = 0;
static unsigned long pressStartTime = 0;
static unsigned long releaseTime = 0;
static bool lastButtonState = HIGH;
static bool buttonState = HIGH;
static ButtonState state = BUTTON_STATE_IDLE;
```

**Constantes critiques**:
- `DEBOUNCE_MS = 50` : Anti-rebond
- `LONG_PRESS_MS = 1000` : Seuil appui long
- `DOUBLE_PRESS_WINDOW_MS = 400` : Fenêtre double-clic

### Module Display (display.cpp/h)

**Responsabilité**: Gestion affichage TFT et modes d'affichage

**États du système**:
```cpp
// Modes d'affichage
enum DisplayMode {
  DISPLAY_MODE_AUTO,    // Luminosité adaptative selon heure
  DISPLAY_MODE_MANUAL,  // Contrôle utilisateur total
  DISPLAY_MODE_OFF      // Toujours éteint
};

// Niveaux de luminosité
enum BrightnessLevel {
  BRIGHTNESS_OFF = 0,
  BRIGHTNESS_NIGHT = 20,
  BRIGHTNESS_LOW = 60,
  BRIGHTNESS_MED = 120,
  BRIGHTNESS_HIGH = 200
};
```

**Variables d'état partagées** (CRITICAL - niveau fichier):
```cpp
static DisplayMode currentDisplayMode = DISPLAY_MODE_MANUAL;
static BrightnessLevel currentBrightness = BRIGHTNESS_MED;
static BrightnessLevel manualBrightness = BRIGHTNESS_MED;
static bool manualPowerState = true;

// Variables de navigation (partagées entre updateDisplayCycle et nextDisplayView)
static int currentViewIdx = 0;
static unsigned long lastViewSwitch = 0;
static int lastModeCount = 0;
```

**Logique AUTO brightness**:
```cpp
BrightnessLevel getAutoBrightness() {
  if (!g_timeValid) return BRIGHTNESS_MED;

  int h = g_timeinfo.tm_hour;
  int m = g_timeinfo.tm_min;

  if ((h == 23 && m >= 30) || h < 7)  return BRIGHTNESS_OFF;    // 23:30-7:00
  else if (h == 22 || (h == 23 && m < 30)) return BRIGHTNESS_NIGHT; // 22:00-23:30
  else if (h >= 18 && h < 22)         return BRIGHTNESS_MED;    // 18:00-22:00
  else                                return BRIGHTNESS_HIGH;   // 7:00-18:00
}
```

**Cycle de vues**:
```cpp
// Vues disponibles selon données
modes[0] = 0;  // Toujours: Valeurs actuelles
if (hasWeatherData()) modes[n++] = 3;  // Météo
if (hasNightData() && getNightMaxCO2() > 0) modes[n++] = 1;  // Nuit
if (hasDayData() && getDayMaxCO2() > 0) modes[n++] = 2;  // Jour

// Durées d'affichage
viewDuration = (mode == 0) ? 10000 : 5000;  // Actuelles: 10s, autres: 5s
```

### Module Connectivity (connectivity.cpp/h)

**Responsabilité**: Réseau (WiFi, MQTT, OTA)

**Gestion reconnexion WiFi**:
```cpp
if (WiFi.status() != WL_CONNECTED) {
  static unsigned long lastWiFiReconnect = 0;
  if (millis() - lastWiFiReconnect >= WIFI_RETRY_MS) {  // 10s
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASS);
  }
}
```

**Gestion reconnexion MQTT**:
```cpp
if (!mqttClient.connected()) {
  static unsigned long lastMqttReconnect = 0;
  if (millis() - lastMqttReconnect >= MQTT_RETRY_MS) {  // 5s
    String clientId = "WeatherProbe-" + WiFi.macAddress();
    if (mqttClient.connect(clientId.c_str(), MQTT_USER, MQTT_PASS)) {
      // S'abonner aux topics de commande
      mqttClient.subscribe("air_analyzer/display/mode/set");
      mqttClient.subscribe("air_analyzer/display/brightness/set");
      mqttClient.subscribe("air_analyzer/display/power/set");
      publishDisplayState();
    }
  }
}
```

**Format publication MQTT** (Home Assistant compatible):
```cpp
void publishDisplayState() {
  // Mode: texte (auto/manual/off)
  const char* modeStr = (mode == AUTO) ? "auto" :
                        (mode == MANUAL) ? "manual" : "off";
  mqttClient.publish("air_analyzer/display/mode", modeStr, true);

  // Brightness: texte (off/night/low/med/high)
  const char* brightStr =
    (brightness <= 0) ? "off" :
    (brightness <= 20) ? "night" :
    (brightness <= 60) ? "low" :
    (brightness <= 120) ? "med" : "high";
  mqttClient.publish("air_analyzer/display/brightness", brightStr, true);

  // Power: texte (on/off)
  mqttClient.publish("air_analyzer/display/power",
                     isDisplayPoweredOn() ? "on" : "off", true);
}
```

**Séquence OTA**:
```
1. ArduinoOTA.handle() détecte connexion
2. onStart():
   - otaInProgress = true
   - esp_task_wdt_delete(NULL)  // Désactiver watchdog
   - mqttClient.disconnect()    // Libérer mémoire
   - stopSensor()               // Libérer mémoire
3. onProgress(): Afficher % tous les 10%
4. onEnd(): Reboot automatique
5. onError(): Afficher erreur + ESP.restart()
```

### Module Sensor (sensor.cpp/h)

**Responsabilité**: Lecture capteur SCD40

**Séquence de lecture** (toutes les 5 minutes):
```cpp
1. scd4x.readMeasurement(co2, temp, hum)
2. if (co2 == 0) → Ignore (capteur pas prêt)
3. temp -= TEMP_OFFSET  // Calibration -1.7°C
4. lastCO2 = co2, lastTemp = temp, lastHum = hum
5. updateStats(co2, temp, hum)  // Min/max jour/nuit
6. publishSensorData(co2, temp, hum)  // MQTT
```

**Calibration température**:
```cpp
#define TEMP_OFFSET -1.0f  // SCD40 lit ~1.0°C trop froid
// Raison: Environnement du capteur et éloignement de l'ESP32
// Offset négatif = le capteur ajoute cette valeur
```

### Module Stats (stats.cpp/h)

**Responsabilité**: Calcul min/max jour/nuit

**Logique de réinitialisation**:
```cpp
// Reset à 7h00 (début de journée)
if (current_hour == 7 && !dayResetDone) {
  resetDayStats();
  dayResetDone = true;
}

// Reset à 22h00 (début de nuit)
if (current_hour == 22 && !nightResetDone) {
  resetNightStats();
  nightResetDone = true;
}
```

### Module Utils (utils.cpp/h)

**Responsabilité**: Services transversaux

**Watchdog**:
```cpp
void initWatchdog() {
  esp_task_wdt_config_t config = {
    .timeout_ms = WDT_TIMEOUT_S * 1000,  // 60s
    .idle_core_mask = 0,
    .trigger_panic = true
  };
  esp_task_wdt_init(&config);
  esp_task_wdt_add(NULL);
}

void feedWatchdog() {
  esp_task_wdt_reset();  // Appelé chaque loop
}
```

**NTP Time sync**:
```cpp
void updateTime() {
  static unsigned long lastTimeUpdate = 0;
  if (millis() - lastTimeUpdate >= 86400000) {  // 1x par jour
    if (getLocalTime(&g_timeinfo)) {
      g_timeValid = true;
    }
  }
}
```

### Module Weather (weather.cpp/h)

**Responsabilité**: Récupération prévisions météo

**API utilisée**: Open-Meteo (gratuit, sans clé)
```
GET https://api.open-meteo.com/v1/forecast?
    latitude=XX.XX&longitude=XX.XX&
    daily=weathercode,temperature_2m_max,temperature_2m_min&
    timezone=auto&forecast_days=2
```

**Parsing JSON**:
```cpp
StaticJsonDocument<2048> doc;
deserializeJson(doc, httpClient.getString());

weatherCode = doc["daily"]["weathercode"][day];
weatherMaxTemp = doc["daily"]["temperature_2m_max"][day];
weatherMinTemp = doc["daily"]["temperature_2m_min"][day];
```

**Sélection jour**:
```cpp
// Si après 18h, afficher demain
int day = (g_timeinfo.tm_hour >= 18) ? 1 : 0;
```

## Gestion de la mémoire

### Allocation statique vs dynamique

**Statique** (préféré pour stabilité):
```cpp
static WiFiClient espClient;          // Allocation compile-time
static PubSubClient mqttClient;
static Adafruit_ST7789 tft;
static SensirionI2CScd4x scd4x;
```

**Dynamique** (limité):
```cpp
String message = "";  // Uniquement dans callbacks courts
StaticJsonDocument<2048> doc;  // Taille fixe, pas de fragmentation
```

### Heap monitoring

**Valeurs typiques**:
- Démarrage: ~200 KB libre
- Fonctionnement normal: ~150 KB libre
- Avant OTA: ~120 KB libre (minimum requis)

**Debug**:
```cpp
Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
```

## Timing et performance

### Budget CPU par loop

```
feedWatchdog()          <1ms
updateTime()            <1ms (sauf sync: ~100ms)
handleWiFiReconnect()   <1ms (sauf reconnect: ~5s)
handleButton()          <1ms
handleSensorRead()      <1ms (sauf lecture: ~50ms toutes les 5min)
handleDisplayUpdate()   5-20ms (selon changement)
handleWeatherFetch()    <1ms (sauf fetch: ~2s toutes les heures)
handleOTA()             <1ms (sauf upload: bloque tout)
handleMQTT()            <5ms

Total nominal: ~30ms par loop → 33 FPS
```

### Optimisations

1. **Redraw partiel** : Seulement les zones modifiées
2. **Debouncing** : Évite lectures GPIO multiples
3. **Timers non-bloquants** : Pas de delay()
4. **Static locals** : Évite allocation/désallocation

## Sécurité et fiabilité

### Watchdog

- Timeout: 60s
- Désactivé pendant OTA
- Reset automatique si blocage

### Gestion erreurs

```cpp
// WiFi
if (WiFi.status() != WL_CONNECTED) {
  // Pas de panic, reconnexion automatique
}

// MQTT
if (!mqttClient.connected()) {
  // Pas de panic, reconnexion automatique
}

// Capteur
if (co2 == 0) {
  // Ignore lecture, retry dans 5min
}

// OTA error
ArduinoOTA.onError([](ota_error_t error) {
  Serial.printf("Error: %u\n", error);
  ESP.restart();  // Reboot pour récupérer
});
```

### Secrets

Fichier `secrets.h` (ignoré par git):
```cpp
#define WIFI_SSID "..."
#define WIFI_PASS "..."
#define MQTT_SERVER "..."
#define MQTT_USER "..."
#define MQTT_PASS "..."
#define OTA_PASSWORD "..."
```

## Diagramme de dépendances complètes

```
config.h ────────────────────────> [Tous les modules]
  │
secrets.h ──────────────────────> connectivity.cpp
  │
version.h ──────────────────────> display.cpp
  │
utils.cpp/h ────────────────────> [Tous les modules]
  │
sensor.cpp/h ───┬──────────────> stats.cpp/h
                └──────────────> display.cpp
                │
stats.cpp/h ────┴──────────────> display.cpp
                │
weather.cpp/h ──┴──────────────> display.cpp
                │
button.cpp/h ───┴──────────────> air_analyzer.ino ──> display.cpp
                │                                  └──> connectivity.cpp
connectivity.cpp/h ─────────────> display.cpp (getters/setters)
                │                 air_analyzer.ino (publishDisplayState)
                │
display.cpp/h ──────────────────> [Aucune dépendance sortante sauf sensors/stats/weather]
```

## Évolution future

### Ajouts possibles

1. **Capteur de lumière ambiante** → Luminosité auto réelle
2. **Graphiques historiques** → Afficher courbes sur écran
3. **Alarmes CO2** → Buzzer si seuil dépassé
4. **Deep sleep** → Économie énergie (incompatible avec serveur MQTT)
5. **SD Card** → Logging local

### Contraintes à respecter

- **Flash**: Max ~1.5 MB de code
- **RAM**: Garder >100 KB libre pour OTA
- **CPU**: Boucle < 100ms pour réactivité bouton
- **Réseau**: Limiter requêtes HTTP (1x/heure max)

## Auteur

Laurent R. - 2026
