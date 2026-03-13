# Instructions pour agents IA - Air Analyzer

Ce document fournit le contexte nécessaire pour comprendre et modifier ce projet Arduino efficacement.

## Vue d'ensemble

**Type de projet**: Firmware embarqué ESP32-C3 pour station de surveillance de qualité de l'air
**Langage**: C++ (Arduino framework)
**Architecture**: Modulaire avec séparation des responsabilités

## Structure des modules

```
air_analyzer.ino     → Point d'entrée (setup/loop)
├── button.cpp/h     → Détection événements bouton (debouncing, long/double press)
├── config.h         → Configuration matérielle (pins, constantes)
├── connectivity.cpp → WiFi, MQTT, OTA, callbacks
├── display.cpp      → Contrôle affichage TFT et modes (AUTO/MANUAL/OFF)
├── sensor.cpp       → Lecture SCD40 I2C (CO2, temp, humidité)
├── stats.cpp        → Calcul min/max jour/nuit
├── utils.cpp        → Watchdog, horloge NTP
└── weather.cpp      → API météo Open-Meteo
```

## Dépendances entre modules

```
main (ino)
  ├─> button → display (nextDisplayView, toggleDisplayPower)
  ├─> connectivity → display (publishDisplayState, getters/setters)
  ├─> display → sensor (hasValidData, getLastCO2/Temp/Hum)
  ├─> display → stats (getNightMaxCO2, getDayMaxTemp, etc.)
  ├─> display → weather (hasWeatherData, getWeatherCode, etc.)
  ├─> sensor → stats (updateStats appelé après lecture)
  └─> utils (feedWatchdog, updateTime) → tous les modules
```

## Patterns importants

### 1. Variables statiques au niveau fichier (CRITIQUE)

**Problème résolu**: Les variables de navigation `currentViewIdx` et `lastViewSwitch` DOIVENT être au niveau fichier, pas dans les fonctions.

```cpp
// ✅ CORRECT (display.cpp lignes 35-37)
static int currentViewIdx = 0;
static unsigned long lastViewSwitch = 0;

static void updateDisplayCycle() {
  // Utilise currentViewIdx
}

void nextDisplayView() {
  // Partage le même currentViewIdx
}
```

```cpp
// ❌ INCORRECT - Ne jamais faire ça
static void updateDisplayCycle() {
  static int currentViewIdx = 0;  // Variable locale
}

void nextDisplayView() {
  static int currentViewIdx = 0;  // Autre variable, pas partagée!
}
```

### 2. Mode d'affichage par défaut

**Choix important**: Le mode par défaut est `DISPLAY_MODE_MANUAL` (ligne 29 de display.cpp)
- Raison: Évite l'extinction automatique la nuit (peut dérouter l'utilisateur)
- Ne pas changer sans justification

### 3. GPIO strapping pins ESP32-C3

**Piège à éviter**: GPIO 8 est un strapping pin
- Utilisé pour le mode boot
- Peut causer des conflits → Utiliser GPIO 9 à la place
- Configuration actuelle: `BUTTON_PIN 9` (config.h:25)

### 4. Callbacks MQTT asynchrones

Les callbacks MQTT s'exécutent dans un contexte différent:
```cpp
static void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // Convertir payload en String
  String message = "";
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  // Traiter la commande
}
```

### 5. OTA et gestion mémoire

Pendant OTA, tout est arrêté sauf ArduinoOTA.handle():
```cpp
ArduinoOTA.onStart([]() {
  otaInProgress = true;
  esp_task_wdt_delete(NULL);  // Désactiver watchdog
  mqttClient.disconnect();    // Libérer mémoire
  stopSensor();               // Libérer mémoire
});
```

## Pièges courants

### 1. Modification de luminosité sans effet

**Symptôme**: `setDisplayBrightness()` appelé mais rien ne change
**Cause**: En mode AUTO, la luminosité est recalculée à chaque cycle
**Solution**: Passer en MANUAL avant de changer la luminosité

### 2. MQTT "Unknown" dans Home Assistant

**Symptôme**: Les valeurs s'affichent "Inconnu" dans HA
**Cause**: Mismatch entre format publié et options YAML
**Solution**: Publier en format texte ("off", "night", "low", "med", "high")

### 3. Bouton ne répond pas

**Cause probable**:
1. GPIO incorrect (utiliser 9, pas 8)
2. Pull-up non activé (vérifier `pinMode(BUTTON_PIN, INPUT_PULLUP)`)
3. Branchement inversé (bouton doit relier GPIO → GND)

### 4. Watchdog timeout pendant OTA

**Solution**: Le watchdog est automatiquement désactivé dans `onStart()`
**Important**: Ne jamais réduire `WDT_TIMEOUT_S` sous 60s (OTA peut prendre 30-40s)

## Timers et cycles

```
SENSOR_INTERVAL_MS    = 300000   (5 min)  → Lecture capteur
DISPLAY_INTERVAL_MS   = 250      (250ms)  → Rafraîchissement display
WEATHER_INTERVAL_MS   = 3600000  (1h)     → Récupération météo
MQTT_RETRY_MS         = 5000     (5s)     → Reconnexion MQTT
WIFI_RETRY_MS         = 10000    (10s)    → Reconnexion WiFi
VIEW_CYCLE_MS         = 5000     (5s)     → Changement de vue auto
                      = 10000    (10s)    → Vue "valeurs actuelles"
```

## État des modes d'affichage

### Mode AUTO
- Luminosité adaptée selon heure (getAutoBrightness)
- 23h30-7h: Écran éteint
- Bouton power: Sans effet
- Bouton brightness: Sans effet

### Mode MANUAL
- Luminosité fixe (manualBrightness)
- Power contrôlable (manualPowerState)
- Pas d'extinction automatique
- Tous les contrôles fonctionnent

### Mode OFF
- Écran toujours éteint
- Tous les contrôles: Sans effet

## Tests recommandés

### Test 1: Bouton physique
```cpp
// Dans Serial Monitor au boot:
"Bouton initialisé sur GPIO 9, état: HIGH (non appuyé)"

// Appui court → Changement de vue immédiat
// Double appui → Cycle luminosité + passage en MANUAL
// Appui long (mode MANUAL) → Toggle power
```

### Test 2: MQTT
```bash
# S'abonner à tous les topics
mosquitto_sub -h <MQTT_SERVER> -u <USER> -P <PASS> -t "air_analyzer/#" -v

# Tester changement de mode
mosquitto_pub -h <MQTT_SERVER> -u <USER> -P <PASS> -t "air_analyzer/display/mode/set" -m "manual"

# Tester luminosité
mosquitto_pub -h <MQTT_SERVER> -u <USER> -P <PASS> -t "air_analyzer/display/brightness/set" -m "high"
```

### Test 3: OTA
```bash
# Via Arduino IDE: Sélectionner port "air-analyzer at xxx.xxx.xxx.xxx"
# Via PlatformIO CLI:
pio run --target upload --upload-port air-analyzer.local
```

## Déploiement

### 1. Compilation locale
```bash
# Incrémenter BUILD_NUMBER dans version.h
# Compiler via Arduino IDE ou PlatformIO
```

### 2. Git workflow
```bash
git add .
git commit -m "feat/fix: Description"
git push origin main
```

### 3. Déploiement OTA
```bash
# Option 1: Arduino IDE (Tools → Port → air-analyzer)
# Option 2: PlatformIO
pio run --target upload --upload-port air-analyzer.local
```

## Secrets et sécurité

**Fichier ignoré**: `secrets.h` (défini dans .gitignore)
```cpp
#define WIFI_SSID "..."
#define WIFI_PASS "..."
#define MQTT_SERVER "..."
#define MQTT_USER "..."
#define MQTT_PASS "..."
#define OTA_PASSWORD "..."
```

**Important**: Toujours vérifier que secrets.h n'est jamais commité

## Fichiers à NE PAS commiter

**RÈGLE IMPORTANTE**: Les fichiers suivants ne doivent JAMAIS être committés sur Git :

1. **secrets.h** - Contient identifiants WiFi, MQTT, OTA (déjà dans .gitignore)
2. **homeassistant*.yaml** - Configurations Home Assistant spécifiques à l'utilisateur
3. **homeassistant*.md** - Documentation Home Assistant personnalisée

**Raison**: Ces fichiers contiennent des configurations spécifiques à chaque installation et ne font pas partie du firmware ESP32. Ils sont fournis comme exemples/templates pour l'utilisateur final mais ne doivent pas être versionnés dans le dépôt du projet.

## Modification du code

### Ajouter un nouveau mode d'affichage

1. Ajouter enum dans `display.h`
2. Créer fonction `displayXXXUI()` dans `display.cpp`
3. Ajouter case dans `updateDisplayCycle()`
4. Ajouter case dans `nextDisplayView()`

### Ajouter une commande MQTT

1. Ajouter topic dans `mqttCallback()` (connectivity.cpp)
2. S'abonner au topic dans `handleMQTT()` après connexion
3. Publier l'état dans `publishDisplayState()` si applicable
4. Ajouter entité dans Home Assistant YAML

### Modifier les seuils de luminosité

1. Éditer enum `BrightnessLevel` dans display.h
2. Mettre à jour `getAutoBrightness()` pour les plages horaires
3. Mettre à jour cycle dans `handleButton()` (air_analyzer.ino)
4. Synchroniser options dans Home Assistant YAML

## Contraintes mémoire

- **Flash disponible**: ~1.3 MB restant sur 2 MB
- **RAM dynamique**: ~180 KB utilisé sur 400 KB
- **Heap libre minimum**: 100 KB (requis pour OTA)

**Avant d'ajouter des fonctionnalités**:
```cpp
Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
```

Si < 120 KB, envisager d'optimiser ou supprimer des features.

## Debug serial

**Important**: Tous les messages de debug utilisent Serial.printf()
- Vitesse: 115200 baud
- Observer au démarrage pour vérifier l'initialisation
- MQTT callbacks affichent toutes les commandes reçues

## Ressources externes

- [ESP32-C3 Datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-c3_datasheet_en.pdf)
- [SCD40 Datasheet](https://www.sensirion.com/en/environmental-sensors/carbon-dioxide-sensors/carbon-dioxide-sensor-scd4x/)
- [ST7789 Library](https://github.com/adafruit/Adafruit-ST7735-Library)
- [PubSubClient](https://github.com/knolleary/pubsubclient)

## Auteur

Laurent R. - 2026
