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

#endif // CONNECTIVITY_H
