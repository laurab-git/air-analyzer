#include <Arduino.h>
#include "button.h"
#include "config.h"

// ============================================================
// --- ÉTAT DU BOUTON ---
// ============================================================
static unsigned long lastPressTime = 0;
static unsigned long pressStartTime = 0;
static bool isPressed = false;
static bool wasPressed = false;
static int pressCount = 0;
static unsigned long firstPressTime = 0;

// ============================================================
// --- INITIALISATION ---
// ============================================================
void initButton() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  // Alternative: utiliser touchRead() pour GPIO tactile si disponible

  // Test initial du bouton
  delay(10);
  bool initialState = digitalRead(BUTTON_PIN);
  Serial.printf("Bouton initialisé sur GPIO %d, état: %s\n",
                BUTTON_PIN, initialState ? "HIGH (non appuyé)" : "LOW (appuyé)");
}

// ============================================================
// --- VÉRIFICATION BOUTON ---
// ============================================================
ButtonEvent checkButton() {
  unsigned long currentMillis = millis();
  bool currentState = (digitalRead(BUTTON_PIN) == LOW); // Actif à LOW (pull-up)

  ButtonEvent event = BUTTON_NONE;

  // Détection front montant (début appui)
  if (currentState && !wasPressed) {
    pressStartTime = currentMillis;
    isPressed = true;
    wasPressed = true;

    // Vérifier si c'est un double appui
    if (pressCount > 0 && (currentMillis - firstPressTime) < DOUBLE_PRESS_WINDOW_MS) {
      pressCount++;
    } else {
      pressCount = 1;
      firstPressTime = currentMillis;
    }
  }

  // Détection front descendant (fin appui)
  if (!currentState && wasPressed) {
    wasPressed = false;
    unsigned long pressDuration = currentMillis - pressStartTime;

    // Appui long détecté
    if (pressDuration >= LONG_PRESS_MS) {
      event = BUTTON_LONG_PRESS;
      pressCount = 0; // Reset compteur
    }
    // Appui court - attendre pour détecter double appui
    else if (pressDuration >= DEBOUNCE_MS) {
      lastPressTime = currentMillis;
      // Pas d'événement immédiat, on attend
    }
  }

  // Vérifier timeout pour double appui
  if (pressCount > 0 && !wasPressed &&
      (currentMillis - firstPressTime) > DOUBLE_PRESS_WINDOW_MS) {

    if (pressCount >= 2) {
      event = BUTTON_DOUBLE_PRESS;
    } else if (pressCount == 1) {
      event = BUTTON_SHORT_PRESS;
    }
    pressCount = 0;
  }

  return event;
}
