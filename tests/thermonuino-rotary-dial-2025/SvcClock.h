#pragma once
#include <Arduino.h>
#include "Logging.h"

/**
 * Service Horloge (sans RTC matériel)
 * - Fonctionne immédiatement avec millis()
 * - Peut être renseigné avec un epoch Unix via SvcClock_setEpoch()
 * - Fournit heure/minute "locales" pour l'UI (LED jour/nuit, etc.)
 *
 * Notes :
 * - SvcClock_now() renvoie des secondes "type Unix" si un epoch a été fixé,
 *   sinon des secondes depuis le boot (valeur relative).
 */

// Init du service
void     SvcClock_init();

// --- Epoch Unix (UTC) ---
bool     SvcClock_hasEpoch();                 // vrai si un epoch valide est fixé
void     SvcClock_setEpoch(uint32_t epochUtc);// fixe l'epoch (secondes Unix)
uint32_t SvcClock_now();                      // secondes Unix si epoch fixé, sinon secondes depuis boot

// --- Pseudo-horloge locale (utile sans epoch) ---
void     SvcClock_setTimeHM(uint8_t hour, uint8_t minute); // définit HH:MM locale (sans gérer date)

// Heure/minute locale 0..23 / 0..59
// - avec epoch : basée sur epoch + tzOffsetHours (défaut 0)
// - sans epoch : basée sur la pseudo-horloge
uint8_t  SvcClock_hourLocal(int8_t tzOffsetHours = 0);
uint8_t  SvcClock_minuteLocal(int8_t tzOffsetHours = 0);
