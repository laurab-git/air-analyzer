#ifndef SENSOR_H
#define SENSOR_H

#include <stdint.h>

// ============================================================
// --- GESTION DU CAPTEUR SCD40 ---
// ============================================================
void initSensor();
void handleSensorRead();

// Getters pour les dernières valeurs
uint16_t getLastCO2();
float getLastTemp();
float getLastHum();
bool hasValidData();

// Arrêt du capteur (pour OTA)
void stopSensor();

#endif // SENSOR_H
