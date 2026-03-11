#include <Arduino.h>
#include "connectivity.h"
#include "config.h"
#include "utils.h"
#include "sensor.h"
#include "secrets.h"
#include <Network.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoOTA.h>
#include <esp_task_wdt.h>

// ============================================================
// --- OBJETS RÉSEAU ---
// ============================================================
static WiFiClient espClient;
static PubSubClient mqttClient(espClient);

// ============================================================
// --- ÉTAT OTA ---
// ============================================================
static bool otaInProgress = false;

// ============================================================
// --- INITIALISATION RÉSEAU ---
// ============================================================
void initNetwork() {
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  WiFi.setSleep(false); // Désactiver le mode veille WiFi pour l'OTA

  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 15000) {
    delay(500);
    Serial.print(".");
    feedWatchdog();
  }

  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Adresse IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("Masque de sous-réseau: ");
    Serial.println(WiFi.subnetMask());
    Serial.print("Passerelle: ");
    Serial.println(WiFi.gatewayIP());
    Serial.print("MAC: ");
    Serial.println(WiFi.macAddress());

    // Configuration timezone
    configTzTime(TIMEZONE_STR, NTP_SERVER);

    // Configuration MQTT
    mqttClient.setServer(MQTT_SERVER, 1883);

    // Configuration OTA
    ArduinoOTA.setHostname("air-analyzer");
    ArduinoOTA.setPassword(OTA_PASSWORD);

    ArduinoOTA.onStart([]() {
      otaInProgress = true;
      esp_task_wdt_delete(NULL);

      // Libérer un maximum de mémoire
      mqttClient.disconnect();
      stopSensor();

      Serial.println("OTA Start");
      Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
    });

    ArduinoOTA.onEnd([]() {
      Serial.println("\nOTA OK");
    });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
      static unsigned int lastPercent = 0;
      unsigned int percent = (progress / (total / 100));
      if (percent >= lastPercent + 10 || percent == 100) {
        Serial.printf("Progress: %u%%\n", percent);
        lastPercent = percent;
      }
    });

    ArduinoOTA.onError([](ota_error_t error) {
      Serial.printf("OTA Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive");
      else if (error == OTA_END_ERROR) Serial.println("End");
      ESP.restart();
    });

    ArduinoOTA.begin();
    Serial.println("OTA activé");
  } else {
    Serial.println("WiFi ERREUR - Mode hors-ligne");
  }
}

// ============================================================
// --- RECONNEXION WIFI ---
// ============================================================
void handleWiFiReconnect() {
  if (WiFi.status() != WL_CONNECTED) {
    static unsigned long lastWiFiReconnect = 0;
    unsigned long currentMillis = millis();
    if ((unsigned long)(currentMillis - lastWiFiReconnect) >= WIFI_RETRY_MS) {
      lastWiFiReconnect = currentMillis;
      Serial.println("WiFi perdu... Tentative de reconnexion.");
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASS);
    }
  }
}

// ============================================================
// --- GESTION MQTT ---
// ============================================================
void handleMQTT() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  if (!mqttClient.connected()) {
    static unsigned long lastMqttReconnect = 0;
    unsigned long currentMillis = millis();
    if ((unsigned long)(currentMillis - lastMqttReconnect) >= MQTT_RETRY_MS) {
      lastMqttReconnect = currentMillis;

      String clientId = "WeatherProbe-" + String(WiFi.macAddress());
      Serial.printf("Tentative MQTT (%s)... ", clientId.c_str());

      if (mqttClient.connect(clientId.c_str(), MQTT_USER, MQTT_PASS)) {
        Serial.println("connecté !");
      } else {
        int state = mqttClient.state();
        Serial.printf("échec rc=%d%s\n", state,
                      state == 5   ? " (Identifiants incorrects)"
                      : state == 2 ? " (Client ID rejeté)"
                                   : "");
      }
    }
  } else {
    mqttClient.loop();
  }
}

// ============================================================
// --- PUBLICATION MQTT ---
// ============================================================
void publishSensorData(uint16_t co2, float temp, float hum) {
  if (mqttClient.connected()) {
    char payload[128];
    snprintf(payload, sizeof(payload),
             "{\"temperature\":%.2f,\"humidity\":%.2f,\"co2\":%d}", temp,
             hum, co2);
    mqttClient.publish("weather_probe/sensor/state", payload);
    Serial.printf("MQTT publié: %s\n", payload);
  }
}

// ============================================================
// --- GESTION OTA ---
// ============================================================
void handleOTA() {
  if (WiFi.status() == WL_CONNECTED) {
    ArduinoOTA.handle();
  }
}

bool isOTAInProgress() {
  return otaInProgress;
}
