#include <Arduino.h>
#include "sensor.h"
#include "config.h"
#include "stats.h"
#include "connectivity.h"
#include <Wire.h>
#include <SensirionI2cScd4x.h>

// ============================================================
// --- OBJET CAPTEUR ---
// ============================================================
static SensirionI2cScd4x scd4x;

// ============================================================
// --- ÉTAT DU CAPTEUR ---
// ============================================================
static uint16_t lastCO2 = 0;
static float lastTemp = -100.0f;
static float lastHum = -100.0f;
static bool dataValid = false;

static unsigned long lastSensorReadTime = 0;
static bool sensorBootWait = true;

// ============================================================
// --- INITIALISATION ---
// ============================================================
void initSensor() {
  Wire.begin(I2C_SDA, I2C_SCL);
  scd4x.begin(Wire, 0x62);
  scd4x.stopPeriodicMeasurement();
  delay(500);

  // Low Power Mode : mesure toutes les 30s au lieu de 5s
  // Avantage : réduction de l'auto-échauffement → température plus précise
  scd4x.startLowPowerPeriodicMeasurement();

  // Offset de température pour compenser la chaleur de l'ESP32
  scd4x.setTemperatureOffset(TEMP_OFFSET);
}

// ============================================================
// --- LECTURE PÉRIODIQUE ---
// ============================================================
void handleSensorRead() {
  unsigned long currentMillis = millis();

  // Au boot, on attend que le capteur soit prêt plutôt qu'une lecture
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
        sensorBootWait = false; // Première mesure obtenue : rythme normal

        lastCO2 = co2;
        lastTemp = temp;
        lastHum = hum;
        dataValid = true;

        updateStats(co2, temp, hum);
        publishSensorData(co2, temp, hum);
      }
    } else if (!sensorBootWait) {
      // En rythme normal, si le capteur n'est pas prêt on reporte de 5s
      lastSensorReadTime = currentMillis - SENSOR_INTERVAL_MS + 5000UL;
    }
  }
}

// ============================================================
// --- ARRÊT DU CAPTEUR (pour OTA) ---
// ============================================================
void stopSensor() {
  scd4x.stopPeriodicMeasurement();
}

// ============================================================
// --- GETTERS ---
// ============================================================
uint16_t getLastCO2() { return lastCO2; }
float getLastTemp() { return lastTemp; }
float getLastHum() { return lastHum; }
bool hasValidData() { return dataValid; }
