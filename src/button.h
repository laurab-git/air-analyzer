#ifndef BUTTON_H
#define BUTTON_H

#include <stdint.h>

// ============================================================
// --- GESTION DU BOUTON TACTILE ---
// ============================================================

// Types d'événements bouton
enum ButtonEvent {
  BUTTON_NONE,
  BUTTON_SHORT_PRESS,  // Appui court : changer de vue
  BUTTON_LONG_PRESS,   // Appui long : basculer on/off
  BUTTON_DOUBLE_PRESS  // Double appui : cycle luminosité
};

void initButton();
ButtonEvent checkButton();

#endif // BUTTON_H
