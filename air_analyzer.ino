#include "config.h"
#include "utils.h"
#include "display.h"
#include "sensor.h"
#include "weather.h"
#include "stats.h"
#include "connectivity.h"
#include "secrets.h"
#include "version.h"

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);

  // Initialisation des modules
  initWatchdog();
  initDisplay();
  initNetwork();

  feedWatchdog();

  initSensor();

  feedWatchdog();
}

// ============================================================
// LOOP
// ============================================================
void loop() {
  // Si OTA en cours, ne gérer que l'OTA et rien d'autre
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
