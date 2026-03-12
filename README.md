# Air Analyzer

Station de surveillance de la qualité de l'air avec ESP32-C3 et capteur CO2 SCD40.

## Caractéristiques

- **Mesure continue** : CO2, température et humidité toutes les 5 minutes
- **Affichage TFT couleur** : Écran ST7789 1.9" avec cycle de vues automatique
- **Connectivité** : WiFi + MQTT pour intégration domotique
- **Prévisions météo** : Récupération automatique via Open-Meteo API
- **Contrôle flexible** : Bouton physique + commandes MQTT
- **Mise à jour OTA** : Déploiement sans fil du firmware

## Matériel

### Composants principaux

- **Microcontrôleur** : ESP32-C3 SuperMini
- **Capteur** : Sensirion SCD40 (CO2 NDIR, température, humidité)
- **Écran** : ST7789 1.9" TFT 170x320 pixels
- **Bouton** : Bouton tactile ou poussoir (GPIO 8)

### Câblage

#### Écran TFT (SPI)
| Pin écran | Pin ESP32-C3 |
|-----------|--------------|
| VCC       | 3.3V         |
| GND       | GND          |
| CS        | GPIO 0       |
| RES       | GPIO 3       |
| DC        | GPIO 2       |
| BLK       | GPIO 1       |
| SDA       | GPIO 7       |
| SCL       | GPIO 4       |

#### Capteur SCD40 (I2C)
| Pin capteur | Pin ESP32-C3 |
|-------------|--------------|
| VIN         | 3.3V         |
| GND         | GND          |
| SDA         | GPIO 5       |
| SCL         | GPIO 6       |

#### Bouton de contrôle
| Pin bouton  | Pin ESP32-C3 |
|-------------|--------------|
| Un terminal | GPIO 8       |
| Autre       | GND          |

> Note : Le GPIO 8 utilise une résistance pull-up interne, pas besoin de composant externe.

## Modes d'affichage

Le système dispose de **3 modes de fonctionnement** :

### Mode AUTO (par défaut)
Luminosité adaptative selon l'heure :
- **7h-18h** : Luminosité forte (200)
- **18h-22h** : Luminosité moyenne (120)
- **22h-23h30** : Luminosité faible nuit (20)
- **23h30-7h** : Écran éteint

### Mode MANUEL
Contrôle total par l'utilisateur :
- Allumage/extinction manuel
- Choix de la luminosité (5 niveaux : 0, 20, 60, 120, 200)
- Pas d'extinction automatique

### Mode OFF
Écran toujours éteint (économie d'énergie maximale)

## Contrôle par bouton physique

Le bouton permet un contrôle local sans Home Assistant :

| Action | Fonction |
|--------|----------|
| **Appui court** | Passer à la vue suivante |
| **Appui long (1s)** | Allumer/éteindre l'écran (mode manuel uniquement) |
| **Double appui** | Cycle de luminosité : Faible → Moyen → Fort → Nuit |

## Contrôle MQTT (Home Assistant)

### Topics de commande (publier vers)

#### Changer de mode
```
Topic: air_analyzer/display/mode/set
Valeurs: auto | manual | off
```

#### Régler la luminosité (mode manuel)
```
Topic: air_analyzer/display/brightness/set
Valeurs: off | night | low | med | high
ou valeurs numériques: 0 | 20 | 60 | 120 | 200
```

#### Allumer/éteindre (mode manuel)
```
Topic: air_analyzer/display/power/set
Valeurs: on | off
```

### Topics d'état (s'abonner à)

```
air_analyzer/display/mode          # Mode actuel (auto/manual/off)
air_analyzer/display/brightness    # Luminosité actuelle (0-200)
air_analyzer/display/power         # État on/off
```

### Topics de données capteur

```
weather_probe/sensor/state         # JSON: {temperature, humidity, co2}
```

## Configuration Home Assistant

### Exemple de configuration YAML

```yaml
# Configuration MQTT
mqtt:
  switch:
    - name: "Air Analyzer Display"
      state_topic: "air_analyzer/display/power"
      command_topic: "air_analyzer/display/power/set"
      payload_on: "on"
      payload_off: "off"

  select:
    - name: "Air Analyzer Mode"
      state_topic: "air_analyzer/display/mode"
      command_topic: "air_analyzer/display/mode/set"
      options:
        - "auto"
        - "manual"
        - "off"

    - name: "Air Analyzer Brightness"
      state_topic: "air_analyzer/display/brightness"
      command_topic: "air_analyzer/display/brightness/set"
      options:
        - "night"
        - "low"
        - "med"
        - "high"

  sensor:
    - name: "Air Quality CO2"
      state_topic: "weather_probe/sensor/state"
      unit_of_measurement: "ppm"
      value_template: "{{ value_json.co2 }}"

    - name: "Air Temperature"
      state_topic: "weather_probe/sensor/state"
      unit_of_measurement: "°C"
      value_template: "{{ value_json.temperature }}"

    - name: "Air Humidity"
      state_topic: "weather_probe/sensor/state"
      unit_of_measurement: "%"
      value_template: "{{ value_json.humidity }}"
```

## Vues de l'écran

L'écran affiche 4 types de vues en rotation automatique :

1. **Valeurs actuelles** (10 secondes)
   - CO2 en grand avec code couleur (vert/jaune/orange/rouge)
   - Température et humidité
   - Jauge visuelle de qualité de l'air

2. **Extrêmes de la nuit** (5 secondes)
   - Max CO2, Min température, Max humidité

3. **Extrêmes du jour** (5 secondes)
   - Max CO2, Min température, Max humidité

4. **Météo** (5 secondes)
   - Prévisions aujourd'hui ou demain
   - Températures min/max
   - Icône météo

## Seuils de qualité de l'air (CO2)

| Niveau | Seuil | Couleur | Indication |
|--------|-------|---------|------------|
| Excellent | < 800 ppm | Vert | Air frais |
| Bon | 800-1000 ppm | Jaune | Acceptable |
| Moyen | 1000-1500 ppm | Orange | Aérer recommandé |
| Mauvais | > 1500 ppm | Rouge | Aérer immédiatement |

## Installation

### Prérequis

1. Arduino IDE ou PlatformIO
2. Support ESP32 installé
3. Bibliothèques requises :
   - `Adafruit GFX Library`
   - `Adafruit ST7735 and ST7789 Library`
   - `Sensirion I2C SCD4x`
   - `PubSubClient` (MQTT)
   - `ArduinoJson`
   - `WiFi`
   - `ArduinoOTA`

### Configuration

1. Copier `secrets.h.example` vers `secrets.h`
2. Éditer `secrets.h` avec vos identifiants :
   ```cpp
   #define WIFI_SSID "votre_ssid"
   #define WIFI_PASS "votre_mot_de_passe"
   #define MQTT_SERVER "192.168.x.x"
   #define MQTT_USER "mqtt_user"
   #define MQTT_PASS "mqtt_password"
   #define OTA_PASSWORD "ota_password"
   ```

3. Compiler et téléverser via USB

### Mise à jour OTA

Après le premier téléversement USB :

```bash
# Via Arduino IDE : sélectionner le port réseau "air-analyzer"
# Via PlatformIO :
pio run --target upload --upload-port air-analyzer.local
```

## Architecture du code

```
air_analyzer/
├── air_analyzer.ino      # Programme principal
├── button.cpp/h          # Gestion bouton physique
├── config.h              # Configuration matérielle
├── connectivity.cpp/h    # WiFi, MQTT, OTA
├── display.cpp/h         # Affichage TFT et modes
├── secrets.h             # Identifiants (non versionné)
├── sensor.cpp/h          # Lecture capteur SCD40
├── stats.cpp/h           # Calcul statistiques jour/nuit
├── utils.cpp/h           # Fonctions utilitaires (temps, watchdog)
├── version.h             # Version firmware (auto-généré)
└── weather.cpp/h         # Récupération météo
```

## Consommation mémoire

- **Flash** : ~60% (ESP32-C3 2MB)
- **RAM** : ~40% dynamique
- **Heap libre** : >100 KB (suffisant pour OTA)

## Watchdog

Le système utilise un watchdog de 60 secondes pour :
- Redémarrer automatiquement en cas de blocage
- Tolérer les mises à jour OTA (peuvent être longues)
- Garantir la fiabilité 24/7

## Auteur

Laurent B. - 2026

## Licence

MIT License - Libre d'utilisation et de modification
