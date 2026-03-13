#include <Arduino.h>
#include "display.h"
#include "config.h"
#include "utils.h"
#include "sensor.h"
#include "stats.h"
#include "weather.h"
#include "version.h"
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

// ============================================================
// --- OBJET ÉCRAN ---
// ============================================================
static Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

// ============================================================
// --- ÉTAT AFFICHAGE ---
// ============================================================
static bool isDisplayOn = true;
static char currentScreenLabel[32] = "";
static int lastDisplayMinute = -1;
static unsigned long lastDisplayUpdate = 0;
static unsigned long lastCursorToggle = 0;
static bool cursorVisible = true;

// Nouveaux états pour le système de modes
static DisplayMode currentDisplayMode = DISPLAY_MODE_MANUAL;  // Démarrage en mode MANUAL
static BrightnessLevel currentBrightness = BRIGHTNESS_MED;
static BrightnessLevel manualBrightness = BRIGHTNESS_MED;
static bool manualPowerState = true;

// Variables de navigation partagées entre updateDisplayCycle() et nextDisplayView()
static int currentViewIdx = 0;
static unsigned long lastViewSwitch = 0;
static int lastModeCount = 0;

// ============================================================
// --- PROTOTYPES INTERNES ---
// ============================================================
static void updateBacklight(BrightnessLevel level);
static BrightnessLevel getAutoBrightness();
static void updateDisplayCycle();
static void displayUI(uint16_t co2, float t, float h, const char *label);
static void displayWeatherUI(uint8_t code, float maxT, float minT, bool tomorrow);
static void drawWeatherIcon(uint8_t code, int x, int y);

// ============================================================
// --- INITIALISATION ---
// ============================================================
void initDisplay() {
  // Rétroéclairage PWM
  ledcAttach(TFT_BL, 5000, 8);
  ledcWrite(TFT_BL, 120);

  // Écran
  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
  tft.init(170, 320);
  tft.setRotation(1);
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_WHITE);

  // Écran de bienvenue
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_CYAN);
  tft.setTextSize(3);
  tft.setCursor(60, 50);
  tft.println("AIR");
  tft.setCursor(30, 85);
  tft.println("ANALYZER");

  tft.setTextSize(2);
  tft.setTextColor(ST77XX_YELLOW);
  tft.setCursor(110, 130);
  tft.println(FIRMWARE_VERSION);

  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(95, 155);
  tft.print("Build ");
  tft.print(BUILD_NUMBER);
  tft.print(" (");
  tft.print(GIT_HASH);
  tft.println(")");

  delay(2000);
}

// ============================================================
// --- MISE À JOUR AFFICHAGE ---
// ============================================================
void handleDisplayUpdate() {
  unsigned long currentMillis = millis();

  // Rafraîchissement principal
  if ((unsigned long)(currentMillis - lastDisplayUpdate) >= DISPLAY_INTERVAL_MS) {
    lastDisplayUpdate = currentMillis;
    updateDisplayCycle();
  }

  // Curseur clignotant
  if ((unsigned long)(currentMillis - lastCursorToggle) >= 500UL) {
    lastCursorToggle = currentMillis;
    cursorVisible = !cursorVisible;
    tft.setCursor(5, 160);
    tft.setTextSize(1);
    tft.setTextColor(cursorVisible ? ST77XX_WHITE : ST77XX_BLACK);
    tft.print("_");
  }
}

// ============================================================
// --- RÉTROÉCLAIRAGE ---
// ============================================================
static void updateBacklight(BrightnessLevel level) {
  isDisplayOn = (level > BRIGHTNESS_OFF);
  currentBrightness = level;
  ledcWrite(TFT_BL, level);
}

// ============================================================
// --- LUMINOSITÉ AUTOMATIQUE ---
// ============================================================
static BrightnessLevel getAutoBrightness() {
  if (!g_timeValid) {
    return BRIGHTNESS_MED; // Par défaut si pas d'heure
  }

  int currentHour = g_timeinfo.tm_hour;
  int currentMin = g_timeinfo.tm_min;

  // Mode nuit : 23h30 - 7h00 → Éteint
  if ((currentHour == 23 && currentMin >= 30) || currentHour < 7) {
    return BRIGHTNESS_OFF;
  }
  // Soirée : 22h00 - 23h30 → Très faible
  else if (currentHour == 22 || (currentHour == 23 && currentMin < 30)) {
    return BRIGHTNESS_NIGHT;
  }
  // Début de soirée : 18h00 - 22h00 → Moyen
  else if (currentHour >= 18 && currentHour < 22) {
    return BRIGHTNESS_MED;
  }
  // Journée : 7h00 - 18h00 → Fort
  else {
    return BRIGHTNESS_HIGH;
  }
}

// ============================================================
// --- CYCLE D'AFFICHAGE ---
// ============================================================
static void updateDisplayCycle() {
  unsigned long currentMillis = millis();

  // Mode OFF : écran toujours éteint
  if (currentDisplayMode == DISPLAY_MODE_OFF) {
    if (isDisplayOn) {
      updateBacklight(BRIGHTNESS_OFF);
      tft.fillScreen(ST77XX_BLACK);
    }
    return;
  }

  // Attente des premières données
  if (!hasValidData()) {
    // En mode auto, utiliser luminosité auto, sinon la luminosité manuelle
    BrightnessLevel targetBrightness = (currentDisplayMode == DISPLAY_MODE_AUTO)
                                        ? getAutoBrightness()
                                        : (manualPowerState ? manualBrightness : BRIGHTNESS_OFF);

    if (currentBrightness != targetBrightness) {
      updateBacklight(targetBrightness);
    }
    if (isDisplayOn) {
      displayUI(0, 0, 0, "ATTENTE CAPTEUR...");
    }
    return;
  }

  // Déterminer la luminosité cible selon le mode
  BrightnessLevel targetBrightness;
  if (currentDisplayMode == DISPLAY_MODE_AUTO) {
    targetBrightness = getAutoBrightness();
  } else { // DISPLAY_MODE_MANUAL
    targetBrightness = manualPowerState ? manualBrightness : BRIGHTNESS_OFF;
  }

  // Appliquer la luminosité si changement
  if (currentBrightness != targetBrightness) {
    updateBacklight(targetBrightness);
    if (targetBrightness == BRIGHTNESS_OFF) {
      tft.fillScreen(ST77XX_BLACK);
      return;
    }
  }

  // Si écran éteint, pas besoin de mettre à jour le contenu
  if (!isDisplayOn) {
    return;
  }

  // Cycle des vues (valeurs actuelles: 10s, autres: 5s)
  int modes[4];
  int modeCount = 0;
  modes[modeCount++] = 0; // Valeurs actuelles
  if (hasWeatherData())
    modes[modeCount++] = 3;
  if (hasNightData() && getNightMaxCO2() > 0)
    modes[modeCount++] = 1;
  if (hasDayData() && getDayMaxCO2() > 0)
    modes[modeCount++] = 2;

  if (modeCount != lastModeCount) {
    currentViewIdx = 0;
    lastViewSwitch = currentMillis;
    lastModeCount = modeCount;
  } else {
    unsigned long viewDuration = (modes[currentViewIdx] == 0) ? 10000UL : VIEW_CYCLE_MS;
    if ((unsigned long)(currentMillis - lastViewSwitch) >= viewDuration) {
      currentViewIdx = (currentViewIdx + 1) % modeCount;
      lastViewSwitch = currentMillis;
    }
  }

  switch (modes[currentViewIdx]) {
  case 0:
    displayUI(getLastCO2(), getLastTemp(), getLastHum(), "VALEURS ACTUELLES");
    break;
  case 1:
    displayUI(getNightMaxCO2(), getNightMinTemp(), getNightMaxHum(), "EXTREMES DE LA NUIT");
    break;
  case 2:
    displayUI(getDayMaxCO2(), getDayMinTemp(), getDayMaxHum(), "EXTREMES DU JOUR");
    break;
  case 3:
    displayWeatherUI(getWeatherCode(), getWeatherMaxTemp(), getWeatherMinTemp(),
                     isWeatherForTomorrow());
    break;
  }
}

// ============================================================
// --- AFFICHAGE PRINCIPAL CO2/TEMP/HUM ---
// ============================================================
static void displayUI(uint16_t co2, float t, float h, const char *label) {
  static uint16_t prevCO2 = 0xFFFF;
  static float prevT = -100.0f;
  static float prevH = -100.0f;
  static uint16_t prevColor = 0;

  bool labelChanged = (strcmp(label, currentScreenLabel) != 0);

  if (labelChanged) {
    tft.fillScreen(ST77XX_BLACK);
    tft.setCursor(10, 10);
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_WHITE);
    tft.println(label);
    tft.drawFastHLine(10, 115, 300, 0x7BEF);

    tft.setTextSize(2);
    tft.setCursor(215, 90);
    tft.setTextColor(ST77XX_WHITE);
    if (strcmp(label, "VALEURS ACTUELLES") == 0 ||
        strstr(label, "EXTREMES") != NULL)
      tft.print("ppm");

    strncpy(currentScreenLabel, label, sizeof(currentScreenLabel));
    lastDisplayMinute = -1;

    prevCO2 = 0xFFFF;
    prevT = -100.0f;
    prevH = -100.0f;
  }

  // Heure
  if (g_timeValid && (g_timeinfo.tm_min != lastDisplayMinute || labelChanged)) {
    tft.fillRect(250, 10, 60, 20, ST77XX_BLACK);
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(260, 10);
    tft.printf("%02d:%02d", g_timeinfo.tm_hour, g_timeinfo.tm_min);
    lastDisplayMinute = g_timeinfo.tm_min;
  }

  // Couleur et niveau AQI
  uint16_t color = ST77XX_GREEN;
  int aqiLevel = 0;
  if (co2 > 1500) {
    color = ST77XX_RED;
    aqiLevel = 3;
  } else if (co2 > 1000) {
    color = ST77XX_ORANGE;
    aqiLevel = 2;
  } else if (co2 > 800) {
    color = ST77XX_YELLOW;
    aqiLevel = 1;
  }

  // Jauge AQI
  static int lastAqiLevel = -1;
  if (strcmp(label, "VALEURS ACTUELLES") == 0 &&
      (labelChanged || aqiLevel != lastAqiLevel)) {
    int gaugeX = 280, gaugeY = 40, gaugeW = 15, gaugeH = 60;
    tft.fillRect(gaugeX, gaugeY, gaugeW, gaugeH, 0x4A69);
    tft.drawRect(gaugeX - 1, gaugeY - 1, gaugeW + 2, gaugeH + 2, ST77XX_WHITE);
    int fillH = (aqiLevel + 1) * (gaugeH / 4);
    tft.fillRect(gaugeX, gaugeY + (gaugeH - fillH), gaugeW, fillH, color);
    lastAqiLevel = aqiLevel;
  }

  // CO2
  if (labelChanged || co2 != prevCO2 || color != prevColor) {
    tft.setCursor(45, 55);
    tft.setTextSize(6);
    tft.setTextColor(color, ST77XX_BLACK);
    tft.printf("%4u", co2);
    prevCO2 = co2;
    prevColor = color;
  }

  // Température
  if (labelChanged || fabsf(t - prevT) >= 0.1f) {
    tft.setTextSize(2);
    tft.setCursor(20, 135);
    tft.setTextColor(ST77XX_CYAN, ST77XX_BLACK);
    tft.printf("%4.1f C", t);
    prevT = t;
  }

  // Humidité
  if (labelChanged || fabsf(h - prevH) >= 1.0f) {
    tft.setCursor(190, 135);
    tft.setTextColor(ST77XX_MAGENTA, ST77XX_BLACK);
    tft.printf("%3.0f%% HR", h);
    prevH = h;
  }
}

// ============================================================
// --- ICÔNE MÉTÉO ---
// ============================================================
static void drawWeatherIcon(uint8_t code, int x, int y) {
  const uint16_t C_SUN = 0xFFE0;
  const uint16_t C_CLOUD = 0xCE59;
  const uint16_t C_DARKCLOUD = 0x4208;
  const uint16_t C_RAIN = 0x001F;
  const uint16_t C_SNOW = 0xFFFF;

  tft.fillRect(x - 20, y - 15, 40, 45, ST77XX_BLACK);

  if (code <= 1) {
    tft.fillCircle(x, y, 14, C_SUN);
  } else if (code == 2) {
    tft.fillCircle(x - 8, y - 8, 12, C_SUN);
    tft.fillCircle(x, y + 5, 10, C_CLOUD);
    tft.fillCircle(x + 12, y + 5, 12, C_CLOUD);
    tft.fillCircle(x + 5, y - 4, 14, C_CLOUD);
  } else if (code == 3 || code == 45 || code == 48) {
    tft.fillCircle(x - 12, y + 5, 10, C_CLOUD);
    tft.fillCircle(x + 12, y + 5, 12, C_CLOUD);
    tft.fillCircle(x, y - 4, 14, C_CLOUD);
    tft.fillCircle(x, y + 5, 12, C_CLOUD);
  } else if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) {
    tft.fillCircle(x - 12, y, 10, C_DARKCLOUD);
    tft.fillCircle(x + 12, y, 12, C_DARKCLOUD);
    tft.fillCircle(x, y - 9, 14, C_DARKCLOUD);
    tft.drawLine(x - 10, y + 15, x - 14, y + 25, C_RAIN);
    tft.drawLine(x, y + 15, x - 4, y + 25, C_RAIN);
    tft.drawLine(x + 10, y + 15, x + 6, y + 25, C_RAIN);
  } else if ((code >= 71 && code <= 77) || code == 85 || code == 86) {
    tft.fillCircle(x - 12, y, 10, C_CLOUD);
    tft.fillCircle(x + 12, y, 12, C_CLOUD);
    tft.fillCircle(x, y - 9, 14, C_CLOUD);
    tft.fillCircle(x - 10, y + 20, 2, C_SNOW);
    tft.fillCircle(x, y + 24, 2, C_SNOW);
    tft.fillCircle(x + 10, y + 20, 2, C_SNOW);
  } else if (code >= 95) {
    tft.fillCircle(x - 12, y, 10, C_DARKCLOUD);
    tft.fillCircle(x + 12, y, 12, C_DARKCLOUD);
    tft.fillCircle(x, y - 9, 14, C_DARKCLOUD);
    tft.drawLine(x, y + 10, x - 6, y + 20, C_SUN);
    tft.drawLine(x - 6, y + 20, x + 4, y + 20, C_SUN);
    tft.drawLine(x + 4, y + 20, x - 2, y + 30, C_SUN);
  }
}

// ============================================================
// --- ÉCRAN MÉTÉO ---
// ============================================================
static void displayWeatherUI(uint8_t code, float maxT, float minT, bool tomorrow) {
  const char *label = tomorrow ? "METEO DEMAIN" : "METEO AUJOURD'HUI";
  bool labelChanged = (strcmp(label, currentScreenLabel) != 0);

  if (labelChanged) {
    tft.fillScreen(ST77XX_BLACK);
    tft.setCursor(10, 10);
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_WHITE);
    tft.println(label);
    tft.drawFastHLine(10, 115, 300, 0x7BEF);

    tft.setTextSize(2);
    tft.setCursor(150, 45);
    tft.setTextColor(ST77XX_ORANGE);
    tft.print("Max");
    tft.setCursor(150, 85);
    tft.setTextColor(ST77XX_CYAN);
    tft.print("Min");

    drawWeatherIcon(code, 60, 65);

    tft.setTextSize(3);
    tft.setTextColor(ST77XX_ORANGE, ST77XX_BLACK);
    tft.setCursor(200, 40);
    tft.printf("%4.1f", maxT);
    tft.setTextColor(ST77XX_CYAN, ST77XX_BLACK);
    tft.setCursor(200, 80);
    tft.printf("%4.1f", minT);

    strncpy(currentScreenLabel, label, sizeof(currentScreenLabel));
    lastDisplayMinute = -1;
  }

  // Heure
  if (g_timeValid && (g_timeinfo.tm_min != lastDisplayMinute || labelChanged)) {
    tft.fillRect(250, 10, 60, 20, ST77XX_BLACK);
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(260, 10);
    tft.printf("%02d:%02d", g_timeinfo.tm_hour, g_timeinfo.tm_min);
    lastDisplayMinute = g_timeinfo.tm_min;
  }
}

// ============================================================
// --- API PUBLIQUE : CONTRÔLE DES MODES ---
// ============================================================
void setDisplayMode(DisplayMode mode) {
  currentDisplayMode = mode;
  // Forcer une mise à jour immédiate
  updateDisplayCycle();
}

DisplayMode getDisplayMode() {
  return currentDisplayMode;
}

// ============================================================
// --- API PUBLIQUE : CONTRÔLE DE LA LUMINOSITÉ ---
// ============================================================
void setDisplayBrightness(BrightnessLevel level) {
  manualBrightness = level;
  if (currentDisplayMode == DISPLAY_MODE_MANUAL) {
    updateBacklight(manualPowerState ? level : BRIGHTNESS_OFF);
  }
  // Note: En mode AUTO, la luminosité sera appliquée lors du prochain cycle
}

BrightnessLevel getDisplayBrightness() {
  return currentBrightness;
}

// ============================================================
// --- API PUBLIQUE : CONTRÔLE ON/OFF MANUEL ---
// ============================================================
void toggleDisplayPower() {
  if (currentDisplayMode == DISPLAY_MODE_MANUAL) {
    manualPowerState = !manualPowerState;
    updateBacklight(manualPowerState ? manualBrightness : BRIGHTNESS_OFF);
    if (!manualPowerState) {
      tft.fillScreen(ST77XX_BLACK);
    }
  }
}

bool isDisplayPoweredOn() {
  return isDisplayOn;
}

// ============================================================
// --- API PUBLIQUE : NAVIGATION MANUELLE ---
// ============================================================
void nextDisplayView() {
  // Force le passage à la vue suivante dans le cycle
  // Les variables sont partagées au niveau du fichier

  // Récupérer les modes disponibles (même logique que dans updateDisplayCycle)
  int modes[4];
  int modeCount = 0;
  modes[modeCount++] = 0; // Valeurs actuelles
  if (hasWeatherData())
    modes[modeCount++] = 3;
  if (hasNightData() && getNightMaxCO2() > 0)
    modes[modeCount++] = 1;
  if (hasDayData() && getDayMaxCO2() > 0)
    modes[modeCount++] = 2;

  currentViewIdx = (currentViewIdx + 1) % modeCount;
  lastViewSwitch = millis();

  // Afficher immédiatement la nouvelle vue
  switch (modes[currentViewIdx]) {
  case 0:
    displayUI(getLastCO2(), getLastTemp(), getLastHum(), "VALEURS ACTUELLES");
    break;
  case 1:
    displayUI(getNightMaxCO2(), getNightMinTemp(), getNightMaxHum(), "EXTREMES DE LA NUIT");
    break;
  case 2:
    displayUI(getDayMaxCO2(), getDayMinTemp(), getDayMaxHum(), "EXTREMES DU JOUR");
    break;
  case 3:
    displayWeatherUI(getWeatherCode(), getWeatherMaxTemp(), getWeatherMinTemp(),
                     isWeatherForTomorrow());
    break;
  }
}
