#include "aci.h"
#include <Arduino.h>

// ============================================================
// --- ÉTAT INTERNE ---
// ============================================================
static float s_co2Score  = 0.0f;
static float s_tempScore = 0.0f;
static float s_humScore  = 0.0f;
static float s_aciScore  = 0.0f;
static bool  s_hasData   = false;

// ============================================================
// --- SOUS-SCORE CO2 (réf. ANSES / WELL Building Standard) ---
//   ≤600 ppm  → 100  (WELL Gold)
//   600-800   → 100→80
//   800-1000  → 80→60  (seuil vigilance ANSES)
//   1000-1500 → 60→30  (seuil action ANSES / impact cognitif Allen 2016)
//   1500-2000 → 30→10  (seuil alerte ANSES)
//   >2000     → 10→0
// ============================================================
static float computeCO2Score(uint16_t co2) {
  if (co2 <= 600)  return 100.0f;
  if (co2 <= 800)  return 100.0f - (co2 - 600)  * 0.10f;
  if (co2 <= 1000) return 80.0f  - (co2 - 800)  * 0.10f;
  if (co2 <= 1500) return 60.0f  - (co2 - 1000) * 0.06f;
  if (co2 <= 2000) return 30.0f  - (co2 - 1500) * 0.04f;
  float v = 10.0f - (co2 - 2000) * 0.01f;
  return v < 0.0f ? 0.0f : v;
}

// ============================================================
// --- SOUS-SCORE TEMPÉRATURE (réf. ISO 7730 / EN 16798-1) ---
//   20-24°C  → 100  (zone optimale résidentielle)
//   18-20    → 70→100
//   24-26    → 100→70
//   16-18    → 30→70
//   26-28    → 70→30
//   <16 / >28 → 30→0
// ============================================================
static float computeTempScore(float t) {
  if (t >= 20.0f && t <= 24.0f) return 100.0f;
  if (t >= 18.0f && t <  20.0f) return 70.0f  + (t - 18.0f) * 15.0f;
  if (t >  24.0f && t <= 26.0f) return 100.0f - (t - 24.0f) * 15.0f;
  if (t >= 16.0f && t <  18.0f) return 30.0f  + (t - 16.0f) * 20.0f;
  if (t >  26.0f && t <= 28.0f) return 70.0f  - (t - 26.0f) * 20.0f;
  float v = (t < 16.0f) ? 30.0f - (16.0f - t) * 10.0f
                         : 30.0f - (t - 28.0f) * 10.0f;
  return v < 0.0f ? 0.0f : v;
}

// ============================================================
// --- SOUS-SCORE HUMIDITÉ (réf. ASHRAE 55 / ANSES) ---
//   40-60% → 100  (zone optimale)
//   30-40  → 60→100
//   60-70  → 100→60
//   20-30  → 20→60
//   70-80  → 60→20
//   <20 / >80 → 20→0
// ============================================================
static float computeHumScore(float h) {
  if (h >= 40.0f && h <= 60.0f) return 100.0f;
  if (h >= 30.0f && h <  40.0f) return 60.0f  + (h - 30.0f) * 4.0f;
  if (h >  60.0f && h <= 70.0f) return 100.0f - (h - 60.0f) * 4.0f;
  if (h >= 20.0f && h <  30.0f) return 20.0f  + (h - 20.0f) * 4.0f;
  if (h >  70.0f && h <= 80.0f) return 60.0f  - (h - 70.0f) * 4.0f;
  float v = (h < 20.0f) ? 20.0f - (20.0f - h) * 2.0f
                         : 20.0f - (h - 80.0f) * 2.0f;
  return v < 0.0f ? 0.0f : v;
}

// ============================================================
// --- CALCUL DU SCORE COMPOSITE ---
//   ACI = CO2×40% + Temp×35% + Hum×25%
// ============================================================
void updateACI(uint16_t co2, float temp, float hum) {
  s_co2Score  = computeCO2Score(co2);
  s_tempScore = computeTempScore(temp);
  s_humScore  = computeHumScore(hum);
  s_aciScore  = s_co2Score  * 0.40f
              + s_tempScore * 0.35f
              + s_humScore  * 0.25f;
  s_hasData   = true;

  Serial.printf("ACI: score=%.1f (CO2=%.1f Temp=%.1f Hum=%.1f)\n",
                s_aciScore, s_co2Score, s_tempScore, s_humScore);
}

// ============================================================
// --- GETTERS ---
// ============================================================
float getACIScore()    { return s_aciScore; }
float getACICO2Score() { return s_co2Score; }
float getACITempScore(){ return s_tempScore; }
float getACIHumScore() { return s_humScore; }
bool  hasACIData()     { return s_hasData; }

const char* getACILabel() {
  if (s_aciScore >= 80.0f) return "Excellent";
  if (s_aciScore >= 60.0f) return "Bon";
  if (s_aciScore >= 40.0f) return "Moyen";
  if (s_aciScore >= 20.0f) return "Mauvais";
  return "Critique";
}
