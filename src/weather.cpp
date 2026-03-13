#include <Arduino.h>
#include "weather.h"
#include "config.h"
#include "utils.h"
#include "secrets.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ============================================================
// --- ÉTAT MÉTÉO ---
// ============================================================
static uint8_t weatherCode = 0;
static float weatherMaxTemp = 0.0f;
static float weatherMinTemp = 0.0f;
static bool weatherDataAvailable = false;
static bool isWeatherTomorrow = false;

static unsigned long lastWeatherFetch = 0;

// ============================================================
// --- RÉCUPÉRATION MÉTÉO ---
// ============================================================
void handleWeatherFetch() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  unsigned long currentMillis = millis();
  if (lastWeatherFetch != 0 &&
      (unsigned long)(currentMillis - lastWeatherFetch) < WEATHER_INTERVAL_MS) {
    return;
  }

  lastWeatherFetch = currentMillis;
  Serial.println("Récupération météo...");

  WiFiClient weatherClient;
  HTTPClient http;

  String url =
      "http://api.open-meteo.com/v1/forecast?latitude=" + String(LATITUDE, 4) +
      "&longitude=" + String(LONGITUDE, 4) +
      "&daily=weather_code,temperature_2m_max,temperature_2m_min" +
      "&timezone=Europe%2FParis";

  http.begin(weatherClient, url);
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);

    if (!err) {
      int currentHour = g_timeValid ? g_timeinfo.tm_hour : 12;
      int dayIndex = (currentHour >= 17) ? 1 : 0;

      weatherCode = doc["daily"]["weather_code"][dayIndex];
      weatherMaxTemp = doc["daily"]["temperature_2m_max"][dayIndex];
      weatherMinTemp = doc["daily"]["temperature_2m_min"][dayIndex];
      isWeatherTomorrow = (dayIndex == 1);
      weatherDataAvailable = true;

      Serial.printf("Météo: Code %d, Max %.1f, Min %.1f\n", weatherCode,
                    weatherMaxTemp, weatherMinTemp);
    } else {
      Serial.printf("Erreur JSON: %s\n", err.c_str());
    }
  } else {
    Serial.printf("Erreur HTTP météo: %d\n", httpCode);
  }
  http.end();
}

// ============================================================
// --- GETTERS ---
// ============================================================
uint8_t getWeatherCode() { return weatherCode; }
float getWeatherMaxTemp() { return weatherMaxTemp; }
float getWeatherMinTemp() { return weatherMinTemp; }
bool isWeatherForTomorrow() { return isWeatherTomorrow; }
bool hasWeatherData() { return weatherDataAvailable; }
