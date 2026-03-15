#ifndef CONNECTIVITY_H
#define CONNECTIVITY_H

#include <stdint.h>

// ============================================================
// --- GESTION RÉSEAU (WiFi / MQTT / OTA) ---
// ============================================================
void initNetwork();
void handleWiFiReconnect();
void handleMQTT();
void handleOTA();
bool isOTAInProgress();

// Publication de données capteur sur MQTT
void publishSensorData(uint16_t co2, float temp, float hum);

// Publication de l'état de l'affichage sur MQTT
void publishDisplayState();

// Publication de l'Indice de Confort (ACI) sur MQTT
// Topics publiés (retained) :
//   air_analyzer/aci/score       → entier 0-100
//   air_analyzer/aci/label       → "Excellent" / "Bon" / "Moyen" / "Mauvais" / "Critique"
//   air_analyzer/aci/co2_score   → sous-score CO2  0-100
//   air_analyzer/aci/temp_score  → sous-score Temp 0-100
//   air_analyzer/aci/hum_score   → sous-score Hum  0-100
void publishACIData();

#endif // CONNECTIVITY_H
