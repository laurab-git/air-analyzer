#include "connectivity.h"
#include "config.h"
#include "secrets.h"
#include "sensor.h"
#include "display.h"
#include "utils.h"
#include <Arduino.h>
#include <ArduinoOTA.h>
#include <Network.h>
#include <PubSubClient.h>
#include <WiFi.h>
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
// --- CALLBACK MQTT ---
// ============================================================
static void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // Convertir le payload en string
  String message = "";
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  Serial.printf("MQTT reçu [%s]: %s\n", topic, message.c_str());

  // Commandes de contrôle de l'affichage
  if (strcmp(topic, "air_analyzer/display/mode/set") == 0) {
    if (message == "auto") {
      Serial.println("MQTT: Passage en mode AUTO");
      setDisplayMode(DISPLAY_MODE_AUTO);
      publishDisplayState();
    } else if (message == "manual") {
      Serial.println("MQTT: Passage en mode MANUAL");
      setDisplayMode(DISPLAY_MODE_MANUAL);
      publishDisplayState();
    } else if (message == "off") {
      Serial.println("MQTT: Passage en mode OFF");
      setDisplayMode(DISPLAY_MODE_OFF);
      publishDisplayState();
    }
  }
  else if (strcmp(topic, "air_analyzer/display/brightness/set") == 0) {
    BrightnessLevel level = BRIGHTNESS_MED;
    if (message == "off" || message == "0") level = BRIGHTNESS_OFF;
    else if (message == "night" || message == "20") level = BRIGHTNESS_NIGHT;
    else if (message == "low" || message == "60") level = BRIGHTNESS_LOW;
    else if (message == "med" || message == "120") level = BRIGHTNESS_MED;
    else if (message == "high" || message == "200") level = BRIGHTNESS_HIGH;
    else {
      // Essayer de parser comme nombre
      int val = message.toInt();
      if (val >= 0 && val <= 255) {
        level = (BrightnessLevel)val;
      }
    }
    Serial.printf("MQTT: Luminosité -> %d\n", (int)level);
    setDisplayBrightness(level);
    // Republier l'état complet (qui convertit en texte)
    publishDisplayState();
  }
  else if (strcmp(topic, "air_analyzer/display/power/set") == 0) {
    if (message == "on" || message == "1") {
      Serial.println("MQTT: Power ON demandé");
      if (getDisplayMode() == DISPLAY_MODE_MANUAL && !isDisplayPoweredOn()) {
        toggleDisplayPower();
        publishDisplayState();
      } else if (getDisplayMode() != DISPLAY_MODE_MANUAL) {
        Serial.println("MQTT: Impossible - pas en mode MANUAL");
      }
    } else if (message == "off" || message == "0") {
      Serial.println("MQTT: Power OFF demandé");
      if (getDisplayMode() == DISPLAY_MODE_MANUAL && isDisplayPoweredOn()) {
        toggleDisplayPower();
        publishDisplayState();
      } else if (getDisplayMode() != DISPLAY_MODE_MANUAL) {
        Serial.println("MQTT: Impossible - pas en mode MANUAL");
      }
    }
  }
}

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
    mqttClient.setCallback(mqttCallback);

    // Configuration OTA
    ArduinoOTA.setHostname("air-analyzer");
    ArduinoOTA.setPassword(OTA_PASSWORD);

    ArduinoOTA.onStart([]() {
      otaInProgress = true;
      // On désactive le watchDog pour éviter le reboot intempestif du
      // contrôleur
      esp_task_wdt_delete(NULL);

      // Libérer un maximum de mémoire
      mqttClient.disconnect();
      stopSensor();

      Serial.println("OTA Start");
      Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
    });

    ArduinoOTA.onEnd([]() { Serial.println("\nOTA OK"); });

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
      if (error == OTA_AUTH_ERROR)
        Serial.println("Auth");
      else if (error == OTA_BEGIN_ERROR)
        Serial.println("Begin");
      else if (error == OTA_CONNECT_ERROR)
        Serial.println("Connect");
      else if (error == OTA_RECEIVE_ERROR)
        Serial.println("Receive");
      else if (error == OTA_END_ERROR)
        Serial.println("End");
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

        // Abonnement aux topics de commande
        mqttClient.subscribe("air_analyzer/display/mode/set");
        mqttClient.subscribe("air_analyzer/display/brightness/set");
        mqttClient.subscribe("air_analyzer/display/power/set");

        // Publication de l'état initial via la fonction dédiée
        publishDisplayState();
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
             "{\"temperature\":%.2f,\"humidity\":%.2f,\"co2\":%d}", temp, hum,
             co2);
    mqttClient.publish("weather_probe/sensor/state", payload);
    Serial.printf("MQTT publié: %s\n", payload);
  }
}

void publishDisplayState() {
  if (mqttClient.connected()) {
    const char* modeStr = (getDisplayMode() == DISPLAY_MODE_AUTO) ? "auto" :
                          (getDisplayMode() == DISPLAY_MODE_MANUAL) ? "manual" : "off";
    mqttClient.publish("air_analyzer/display/mode", modeStr, true);

    // Convertir la luminosité en texte pour Home Assistant
    BrightnessLevel currentBright = getDisplayBrightness();
    const char* brightStr;
    if (currentBright <= BRIGHTNESS_OFF) brightStr = "off";
    else if (currentBright <= BRIGHTNESS_NIGHT) brightStr = "night";
    else if (currentBright <= BRIGHTNESS_LOW) brightStr = "low";
    else if (currentBright <= BRIGHTNESS_MED) brightStr = "med";
    else brightStr = "high";

    mqttClient.publish("air_analyzer/display/brightness", brightStr, true);
    mqttClient.publish("air_analyzer/display/power", isDisplayPoweredOn() ? "on" : "off", true);
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

bool isOTAInProgress() { return otaInProgress; }
