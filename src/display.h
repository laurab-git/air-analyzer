#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>

// ============================================================
// --- MODES D'AFFICHAGE ---
// ============================================================
enum DisplayMode {
  DISPLAY_MODE_AUTO,     // Luminosité adaptative selon l'heure + extinction nocturne
  DISPLAY_MODE_MANUAL,   // Contrôle manuel complet (on/off + luminosité)
  DISPLAY_MODE_OFF       // Écran toujours éteint
};

enum BrightnessLevel {
  BRIGHTNESS_OFF = 0,    // Éteint
  BRIGHTNESS_NIGHT = 20, // Nuit (très faible)
  BRIGHTNESS_LOW = 60,   // Faible
  BRIGHTNESS_MED = 120,  // Moyen
  BRIGHTNESS_HIGH = 200  // Fort
};

// ============================================================
// --- GESTION DE L'AFFICHAGE TFT ---
// ============================================================
void initDisplay();
void handleDisplayUpdate();

// Contrôle des modes
void setDisplayMode(DisplayMode mode);
DisplayMode getDisplayMode();

// Contrôle de la luminosité (mode manuel)
void setDisplayBrightness(BrightnessLevel level);
BrightnessLevel getDisplayBrightness();

// Contrôle on/off manuel
void toggleDisplayPower();
bool isDisplayPoweredOn();

// Navigation manuelle entre les vues
void nextDisplayView();

#endif // DISPLAY_H
