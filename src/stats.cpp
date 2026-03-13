#include <Arduino.h>
#include "stats.h"
#include "utils.h"

// ============================================================
// --- DONNÉES RTC (Persistantes après reboot/deep-sleep) ---
// ============================================================
// Ces variables survivent aux resets logiciels
RTC_DATA_ATTR uint16_t nightMaxCO2 = 0;
RTC_DATA_ATTR float nightMinTemp = 100.0f;
RTC_DATA_ATTR float nightMaxHum = 0.0f;
RTC_DATA_ATTR bool nightDataAvail = false;

RTC_DATA_ATTR uint16_t dayMaxCO2 = 0;
RTC_DATA_ATTR float dayMinTemp = 100.0f;
RTC_DATA_ATTR float dayMaxHum = 0.0f;
RTC_DATA_ATTR bool dayDataAvail = false;

RTC_DATA_ATTR int lastResetHour = -1;

// ============================================================
// --- MISE À JOUR DES STATISTIQUES ---
// ============================================================
void updateStats(uint16_t co2, float t, float h) {
  int currentHour = g_timeValid ? g_timeinfo.tm_hour : 12;
  int currentMin = g_timeValid ? g_timeinfo.tm_min : 0;
  bool isNight = ((currentHour == 23 && currentMin >= 30) || currentHour < 7);

  if (isNight) {
    nightDataAvail = true;
    if (co2 > nightMaxCO2)
      nightMaxCO2 = co2;
    if (t < nightMinTemp)
      nightMinTemp = t;
    if (h > nightMaxHum)
      nightMaxHum = h;
  } else {
    dayDataAvail = true;
    if (co2 > dayMaxCO2)
      dayMaxCO2 = co2;
    if (t < dayMinTemp)
      dayMinTemp = t;
    if (h > dayMaxHum)
      dayMaxHum = h;
  }

  // Reset des stats aux heures charnières (détection de changement d'heure)
  if (g_timeValid && lastResetHour != currentHour) {
    if (currentHour == 23) {
      nightMaxCO2 = co2;
      nightMinTemp = t;
      nightMaxHum = h;
      nightDataAvail = true;
    }
    if (currentHour == 7) {
      dayMaxCO2 = co2;
      dayMinTemp = t;
      dayMaxHum = h;
      dayDataAvail = true;
    }
    lastResetHour = currentHour;
  }
}

// ============================================================
// --- GETTERS ---
// ============================================================
uint16_t getNightMaxCO2() { return nightMaxCO2; }
float getNightMinTemp() { return nightMinTemp; }
float getNightMaxHum() { return nightMaxHum; }
bool hasNightData() { return nightDataAvail; }

uint16_t getDayMaxCO2() { return dayMaxCO2; }
float getDayMinTemp() { return dayMinTemp; }
float getDayMaxHum() { return dayMaxHum; }
bool hasDayData() { return dayDataAvail; }
