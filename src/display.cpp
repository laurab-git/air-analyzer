#include <Arduino.h>
#include "display.h"
#include "config.h"
#include "utils.h"
#include "sensor.h"
#include "stats.h"
#include "weather.h"
#include "aci.h"
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
static void displayACIUI();
static uint16_t aciSubColor(float v);
static void drawACISubScore(int x, int y, const char* name, float val, uint16_t color);

// ============================================================
// --- INITIALISATION ---
// ============================================================
void initDisplay() {
  // Rétroéclairage PWM
  ledcSetup(0, 5000, 8);  // Canal 0, 5kHz, 8 bits
  ledcAttachPin(TFT_BL, 0);
  ledcWrite(0, 120);

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
  ledcWrite(0, level);  // Canal 0
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
  int modes[5];
  int modeCount = 0;
  modes[modeCount++] = 0; // Valeurs actuelles
  modes[modeCount++] = 4; // ACI - Indice de confort (toujours dispo si hasValidData)
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
  case 4:
    displayACIUI();
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
// --- COULEUR D'UN SOUS-SCORE ACI ---
// ============================================================
static uint16_t aciSubColor(float v) {
  if (v >= 80.0f) return ST77XX_GREEN;
  if (v >= 60.0f) return 0xAFE0;         // Vert-jaune
  if (v >= 40.0f) return ST77XX_YELLOW;
  if (v >= 20.0f) return 0xFD20;         // Orange
  return ST77XX_RED;
}

// ============================================================
// --- BARRE DE SOUS-SCORE ACI (50px de large) ---
// ============================================================
static void drawACISubScore(int x, int y, const char* name, float val, uint16_t color) {
  tft.fillRect(x, y, 50, 22, ST77XX_BLACK);
  // Label
  tft.setTextSize(1);
  tft.setTextColor(0x7BEF, ST77XX_BLACK);
  tft.setCursor(x, y);
  tft.print(name);
  // Barre fond + remplissage
  tft.fillRect(x, y + 10, 50, 5, 0x2104);
  int fill = (int)(val * 50.0f / 100.0f);
  if (fill > 0) tft.fillRect(x, y + 10, fill, 5, color);
  // Valeur en %
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  char buf[6];
  snprintf(buf, sizeof(buf), "%d%%", (int)val);
  tft.setCursor(x, y + 17);
  tft.print(buf);
}

// ============================================================
// --- VUE ACI : JAUGE ARC + SOUS-SCORES ---
//
// Layout (320×170, paysage) :
//   y=0-19  : En-tête "CONFORT AIR" + heure
//   y=20    : Ligne de séparation
//   y=22-169: Arc semi-circulaire (cx=160, cy=172, r=100-125)
//             Score (taille 4) + label (taille 2) au centre de l'arc
//             3 barres sous-scores en bas de l'arc (y=148-169)
//
// Gradient arc : rouge (gauche=0) → vert (droite=100)
// La portion remplie correspond au score, le reste en gris foncé.
// ============================================================
static void displayACIUI() {
  static float lastScore = -1.0f;
  static float lastCO2s  = -1.0f;
  static float lastTemps = -1.0f;
  static float lastHums  = -1.0f;

  if (!hasACIData()) return;

  float score = getACIScore();
  float co2s  = getACICO2Score();
  float temps = getACITempScore();
  float hums  = getACIHumScore();

  bool labelChanged = (strcmp("CONFORT AIR", currentScreenLabel) != 0);
  bool scoreChanged = labelChanged || (fabsf(score - lastScore) >= 0.5f);
  bool subChanged   = scoreChanged
                    || fabsf(co2s  - lastCO2s)  >= 0.5f
                    || fabsf(temps - lastTemps) >= 0.5f
                    || fabsf(hums  - lastHums)  >= 0.5f;
  bool timeChanged  = g_timeValid && (g_timeinfo.tm_min != lastDisplayMinute);

  if (!labelChanged && !scoreChanged && !subChanged && !timeChanged) return;

  if (labelChanged) {
    tft.fillScreen(ST77XX_BLACK);
    strncpy(currentScreenLabel, "CONFORT AIR", sizeof(currentScreenLabel));
    lastDisplayMinute = -1;
    timeChanged = true;
  }

  // Couleur principale selon le score global
  uint16_t scoreColor = aciSubColor(score);

  // --- En-tête ---
  if (labelChanged) {
    tft.setTextSize(1);
    tft.setTextColor(0x7BEF, ST77XX_BLACK);
    tft.setCursor(5, 6);
    tft.print("CONFORT AIR");
    tft.drawFastHLine(0, 20, 320, 0x7BEF);
  }

  // --- Heure ---
  if (timeChanged) {
    tft.fillRect(255, 6, 60, 10, ST77XX_BLACK);
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(265, 6);
    tft.printf("%02d:%02d", g_timeinfo.tm_hour, g_timeinfo.tm_min);
    lastDisplayMinute = g_timeinfo.tm_min;
  }

  // --- Arc semi-circulaire ---
  if (scoreChanged) {
    const int cx    = 160;
    const int cy    = 172;   // 2px sous l'écran → le bas de l'arc est invisible
    const int r_in  = 100;
    const int r_out = 125;
    const float filledDeg = score * 1.8f;  // 0-100 → 0-180°

    // Effacer la zone centrale avant de dessiner l'arc (évite le scintillement)
    tft.fillRect(62, 22, 196, 148, ST77XX_BLACK);

    // Dessin de l'arc pixel par pixel (startWrite/writePixel pour performance)
    tft.startWrite();
    for (float deg = 0.0f; deg <= 180.0f; deg += 0.35f) {
      float rad  = deg * 3.14159f / 180.0f;
      float cosV = cosf(rad);
      float sinV = sinf(rad);

      // Gradient rouge→orange→jaune→vert-jaune→vert selon position dans l'arc
      uint16_t color;
      if (deg <= filledDeg) {
        float ratio = deg / 180.0f;
        if      (ratio < 0.20f) color = ST77XX_RED;
        else if (ratio < 0.40f) color = 0xFD20;          // Orange
        else if (ratio < 0.60f) color = ST77XX_YELLOW;
        else if (ratio < 0.80f) color = 0xAFE0;          // Vert-jaune
        else                    color = ST77XX_GREEN;
      } else {
        color = 0x2104;  // Gris très foncé (portion non remplie)
      }

      for (int r = r_in; r <= r_out; r++) {
        int px = cx - (int)((float)r * cosV);
        int py = cy - (int)((float)r * sinV);
        if (px >= 0 && px < 320 && py >= 22 && py < 170)
          tft.writePixel(px, py, color);
      }
    }
    tft.endWrite();

    // Repère blanc à la limite rempli/non-rempli
    if (score > 1.0f && score < 99.0f) {
      float nRad = filledDeg * 3.14159f / 180.0f;
      int nx1 = cx - (int)((r_in  - 5) * cosf(nRad));
      int ny1 = cy - (int)((r_in  - 5) * sinf(nRad));
      int nx2 = cx - (int)((r_out + 5) * cosf(nRad));
      int ny2 = cy - (int)((r_out + 5) * sinf(nRad));
      if (ny1 < 170 && ny2 < 170)
        tft.drawLine(nx1, ny1, nx2, ny2, ST77XX_WHITE);
    }

    // Score (taille 4) centré dans le trou de l'arc (zone r < 100)
    char buf[5];
    snprintf(buf, sizeof(buf), "%d", (int)score);
    int scoreW = strlen(buf) * 24;  // textSize=4 : 6px×4=24 par caractère
    tft.setTextSize(4);
    tft.setTextColor(scoreColor, ST77XX_BLACK);
    tft.setCursor(160 - scoreW / 2, 90);
    tft.print(buf);

    // Label (taille 2) sous le score
    const char* lbl = getACILabel();
    int lblW = strlen(lbl) * 12;  // textSize=2 : 6px×2=12 par caractère
    tft.setTextSize(2);
    tft.setTextColor(scoreColor, ST77XX_BLACK);
    tft.setCursor(160 - lblW / 2, 128);
    tft.print(lbl);

    lastScore = score;
  }

  // --- Sous-scores CO2 / Temp / Hum ---
  if (subChanged) {
    drawACISubScore(67,  148, "CO2", co2s,  aciSubColor(co2s));
    drawACISubScore(135, 148, "TMP", temps, aciSubColor(temps));
    drawACISubScore(203, 148, "HUM", hums,  aciSubColor(hums));
    lastCO2s  = co2s;
    lastTemps = temps;
    lastHums  = hums;
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
  int modes[5];
  int modeCount = 0;
  modes[modeCount++] = 0; // Valeurs actuelles
  modes[modeCount++] = 4; // ACI - Indice de confort
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
  case 4:
    displayACIUI();
    break;
  }
}
