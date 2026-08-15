#pragma once
#include "Services.h"

// ON/OFF de la réception RF (UI coupe la RF pendant l’interaction)
void SvcRF_enable(bool on);

// Appelée par ton décodeur quand une trame valide arrive
// - slot: 0..2 (RF1..RF3)
// - Tq: température en quarts de degré (T°C*4)
// - lowBatt: flag batterie faible
void SvcRF_onReading(uint8_t slot, int16_t Tq, bool lowBatt);

// Poll “gestion d’état” (timeouts + logs). Le décodage brut, c’est toi qui le fais.
void SvcRF_poll();

// Init du service (pins déjà configurées ailleurs si besoin)
void SvcRF_init();
