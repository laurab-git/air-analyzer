#ifndef STATS_H
#define STATS_H

#include <stdint.h>

// ============================================================
// --- STATISTIQUES JOUR/NUIT ---
// ============================================================
// Met à jour les statistiques avec une nouvelle mesure
void updateStats(uint16_t co2, float temp, float hum);

// Getters pour les statistiques de nuit
uint16_t getNightMaxCO2();
float getNightMinTemp();
float getNightMaxHum();
bool hasNightData();

// Getters pour les statistiques de jour
uint16_t getDayMaxCO2();
float getDayMinTemp();
float getDayMaxHum();
bool hasDayData();

#endif // STATS_H
