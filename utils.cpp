#include <Arduino.h>
#include "utils.h"
#include "config.h"
#include <esp_task_wdt.h>
#include <time.h>

// ============================================================
// --- VARIABLES GLOBALES ---
// ============================================================
struct tm g_timeinfo;
bool g_timeValid = false;

// ============================================================
// --- WATCHDOG ---
// ============================================================
void initWatchdog() {
  // Configurer le timeout avant de s'enregistrer
  esp_task_wdt_deinit(); // Désactiver le watchdog par défaut
  esp_task_wdt_config_t wdt_config = {
    .timeout_ms = WDT_TIMEOUT_S * 1000,
    .idle_core_mask = 0,
    .trigger_panic = true
  };
  esp_task_wdt_init(&wdt_config);
  esp_task_wdt_add(NULL); // S'enregistrer avec notre nouveau timeout
}

void feedWatchdog() {
  esp_task_wdt_reset();
}

// ============================================================
// --- GESTION DU TEMPS ---
// ============================================================
void updateTime() {
  // Timeout court (500ms) pour éviter de bloquer la loop pendant le boot NTP
  g_timeValid = getLocalTime(&g_timeinfo, 500);
}
