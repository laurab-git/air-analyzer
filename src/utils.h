#ifndef UTILS_H
#define UTILS_H

#include <time.h>

// ============================================================
// --- GESTION DU TEMPS ---
// ============================================================
// Snapshot de l'heure locale, mis à jour une fois par loop()
extern struct tm g_timeinfo;
extern bool g_timeValid;

// Met à jour g_timeinfo et g_timeValid
void updateTime();

// ============================================================
// --- WATCHDOG ---
// ============================================================
void initWatchdog();
void feedWatchdog();

#endif // UTILS_H
