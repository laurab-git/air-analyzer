#ifndef WEATHER_H
#define WEATHER_H

#include <stdint.h>

// ============================================================
// --- GESTION MÉTÉO (Open-Meteo API) ---
// ============================================================
void handleWeatherFetch();

// Getters pour les données météo
uint8_t getWeatherCode();
float getWeatherMaxTemp();
float getWeatherMinTemp();
bool isWeatherForTomorrow();
bool hasWeatherData();

#endif // WEATHER_H
