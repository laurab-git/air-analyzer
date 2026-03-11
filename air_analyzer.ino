#include "config.h"
#include "connectivity.h"
#include "display.h"
#include "secrets.h"
#include "sensor.h"
#include "stats.h"
#include "utils.h"
#include "version.h"
#include "weather.h"

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);

  // Initialisation des modules
  initWatchdog();
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
  handleSensorRead();
  handleDisplayUpdate();
  handleWeatherFetch();
  handleOTA();
  handleMQTT();
}
