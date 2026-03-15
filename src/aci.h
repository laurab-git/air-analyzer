#ifndef ACI_H
#define ACI_H

#include <stdint.h>

// ============================================================
// --- INDICE DE CONFORT (ACI - Air Comfort Index) ---
// Score composite 0-100 issu de trois sous-scores pondérés :
//   CO2  × 40% (ANSES / WELL Building Standard)
//   Temp × 35% (ISO 7730 / EN 16798-1)
//   Hum  × 25% (ASHRAE 55)
// ============================================================

void        updateACI(uint16_t co2, float temp, float hum);

float       getACIScore();      // Score global  0-100
float       getACICO2Score();   // Sous-score CO2  0-100
float       getACITempScore();  // Sous-score Temp 0-100
float       getACIHumScore();   // Sous-score Hum  0-100
const char* getACILabel();      // "Excellent" / "Bon" / "Moyen" / "Mauvais" / "Critique"
bool        hasACIData();

#endif // ACI_H
