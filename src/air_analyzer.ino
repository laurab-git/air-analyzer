#include <Arduino.h>
#include "config.h"
#include "connectivity.h"
#include "display.h"
#include "button.h"
#include "secrets.h"
#include "sensor.h"
#include "stats.h"
#include "utils.h"
#include "version.h"
#include "weather.h"

// ============================================================
// --- GESTION DU BOUTON ---
// ============================================================
void handleButton() {
  ButtonEvent event = checkButton();

  if (event == BUTTON_SHORT_PRESS) {
    // Appui court : changer de vue manuellement
    nextDisplayView();
    Serial.println("Bouton: Vue suivante");
  }
  else if (event == BUTTON_LONG_PRESS) {
    // Appui long : basculer on/off (mode manuel uniquement)
    if (getDisplayMode() == DISPLAY_MODE_MANUAL) {
      toggleDisplayPower();
      publishDisplayState();
      Serial.println("Bouton: Toggle power");
    }
  }
  else if (event == BUTTON_DOUBLE_PRESS) {
    // Double appui : cycle de luminosité (passe en mode manuel si nécessaire)
    if (getDisplayMode() != DISPLAY_MODE_MANUAL) {
      setDisplayMode(DISPLAY_MODE_MANUAL);
      Serial.println("Bouton: Mode MANUAL activé");
    }

    BrightnessLevel currentLevel = getDisplayBrightness();
    BrightnessLevel newLevel;

    // Cycle: FAIBLE → MOYEN → FORT → NUIT → FAIBLE
    if (currentLevel < BRIGHTNESS_LOW) {
      newLevel = BRIGHTNESS_LOW;
    } else if (currentLevel < BRIGHTNESS_MED) {
      newLevel = BRIGHTNESS_MED;
    } else if (currentLevel < BRIGHTNESS_HIGH) {
      newLevel = BRIGHTNESS_HIGH;
    } else if (currentLevel < BRIGHTNESS_HIGH + 10) {
      newLevel = BRIGHTNESS_NIGHT;
    } else {
      newLevel = BRIGHTNESS_LOW;
    }

    setDisplayBrightness(newLevel);
    publishDisplayState();
    Serial.printf("Bouton: Luminosité -> %d\n", (int)newLevel);
  }
}

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);

  // Initialisation des modules
  initWatchdog();
  initButton();
  initDisplay();
  initNetwork();
  // L'initialisation du Wifi peut être longue. On évite donc le reboot du
  // contrôleur
  feedWatchdog();
  initSensor();
  // L'initialisation du capteur peut être longue. On évite donc le reboot du
  // contrôleur
  feedWatchdog();
}

// ============================================================
// LOOP
// ============================================================
void loop() {
  // Si OTA en cours, ne gérer que l'OTA et rien d'autre
  // Le watchdog est désactivé. Inutile donc de le réinitialiser même si le
  // temps de mise à jour est long
  if (isOTAInProgress()) {
    handleOTA();
    return;
  }

  // Nourrir le watchdog
  feedWatchdog();

  // Mettre à jour l'heure (une fois par cycle)
  updateTime();

  // Gestion des tâches
  handleWiFiReconnect();
  handleButton();
  handleSensorRead();
  handleDisplayUpdate();
  handleWeatherFetch();
  handleOTA();
  handleMQTT();
}
