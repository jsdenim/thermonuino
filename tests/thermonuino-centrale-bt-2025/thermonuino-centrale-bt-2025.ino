// ---- opto_scheduler_15s_dual.ino ----
// Reçoit Statut (datetime + consigne[4]) via Proto v0x02,
// applique un duty-cycle sur 5 min découpé en tranches de 15 s,
// avec au plus 2 zones actives simultanément, et répartition lissée.

// ===== Dépendances =====
#include "protocole_statut.h"   // VER=0x02, payload 8 octets

#define BTN PCINT19
#define LED_STAT 10

#include <TimedBlink.h>
TimedBlink status(LED_STAT);

#include <SoftwareSerial.h>
// On mappe MOSI (PB3) comme TX et MISO (PB4) comme RX
// Broches Arduino : D11 = MOSI (TX), D12 = MISO (RX)
SoftwareSerial debugSerial(12,11); // RX = D12 (MISO), TX = D11 (MOSI)
//L'Atmega328 parle à l'Arduino Uno en 9600bauds, mais l'Arduino uno et l'ordi parlent en 19200 bauds. Ne pas oublier de changer 
//ce réglage dans Serial Monitor, puis faire R pour passer en mode relay.

// ===== Matériel / broches =====
static const uint8_t ZONES = 4;

// Tu avais ces PCINT -> voici les broches Arduino correspondantes :
const uint8_t Z_LED_PINS[ZONES]  = { 6, 7, 8, 9 };       // PCINT22(D6), PCINT23(D7), PCINT0(D8), PCINT1(D9)
const uint8_t Z_OPTO_PINS[ZONES] = { A0, A2, A1, A3 };   // PCINT8(A0), PCINT10(A2), PCINT9(A1), PCINT11(A3)

#define OPTO_ACTIVE_HIGH 1
#define LED_ACTIVE_HIGH  0

// ===== Lien série inter-cartes =====
const unsigned long LINK_BAUD = 1200;

// ===== Paramètres duty-cycle =====
static const uint32_t SLICE_MS   = 15000UL;   // 15 s
static const uint8_t  TICKS      = 20;        // 5 min / 15 s
static const uint8_t  CAPACITY   = 2;         // max 2 zones ON en même temps
static const uint16_t MAX_SLOTS  = TICKS * CAPACITY; // 40

// Failsafe : si plus de statuts pendant X ms, tout couper
static const uint32_t FAILSAFE_MS = 600000UL; // 10 min

// ===== État =====
Proto::Rx rx;
uint8_t consigne_raw[ZONES] = {0,0,0,0};    // 0..255

// Planning de la période courante (calculé à chaque démarrage de période)
uint8_t q_target[ZONES] = {0,0,0,0}; // nb de tranches ON à réaliser (0..20)
uint8_t q_used  [ZONES] = {0,0,0,0}; // nb déjà consommées dans la période
int16_t acc     [ZONES] = {0,0,0,0}; // accumulateurs (peuvent être négatifs)

// Temps / cadence
uint32_t sliceStartMs = 0;
uint8_t  tickIndex    = 0;  // 0..19
uint32_t lastRxMs     = 0;
bool     replanNextSlice = true; // recalculer à la prochaine frontière de tranche

// ===== Utils I/O =====
inline void writeOpto(uint8_t z, bool on) {
  digitalWrite(Z_OPTO_PINS[z], (OPTO_ACTIVE_HIGH ? (on ? HIGH : LOW) : (on ? LOW : HIGH)));
  digitalWrite(Z_LED_PINS[z],  (LED_ACTIVE_HIGH  ? (on ? HIGH : LOW) : (on ? LOW : HIGH)));
}

// ===== Calcul du plan pour une période =====
// 1) consigne[0..255] -> désir "brut" en tranches (0..20) par zone : d_i = round(20 * c/255)
// 2) si somme(d_i) > 40 (capacité), on réduit proportionnellement par
//    plus fort reste pour obtenir exactement 40.
// 3) remise à zéro des compteurs et accumulateurs.
void computePlanFromConsignes() {
  // Étape 1 : désir brut (arrondi au plus proche)
  uint16_t sumDesired = 0;
  uint8_t desire[ZONES];
  for (uint8_t i = 0; i < ZONES; ++i) {
    uint16_t num = (uint16_t)TICKS * consigne_raw[i]; // 20 * c
    uint8_t d = (uint8_t)((num + 127U) / 255U);       // round
    if (d > TICKS) d = TICKS;                         // cap 20
    desire[i] = d;
    sumDesired += d;
  }

  // Étape 2 : adaptation à la capacité (40)
  if (sumDesired <= MAX_SLOTS) {
    for (uint8_t i = 0; i < ZONES; ++i) q_target[i] = desire[i];
  } else {
    // Proportionnel + plus forts restes
    uint16_t sumBase = sumDesired;
    uint16_t sumFloor = 0;
    uint16_t rem[ZONES];

    for (uint8_t i = 0; i < ZONES; ++i) {
      // q' = desire[i] * MAX_SLOTS / sumDesired
      uint32_t num = (uint32_t)desire[i] * (uint32_t)MAX_SLOTS;
      q_target[i] = (uint8_t)(num / sumBase);
      rem[i]      = (uint16_t)(num % sumBase);
      sumFloor   += q_target[i];
    }

    uint16_t leftover = MAX_SLOTS - sumFloor;
    // Distribue les restes les plus grands
    while (leftover > 0) {
      uint8_t best = 0xFF;
      uint16_t bestRem = 0;
      for (uint8_t i = 0; i < ZONES; ++i) {
        if (rem[i] > bestRem) { bestRem = rem[i]; best = i; }
      }
      if (best == 0xFF || bestRem == 0) break;
      if (q_target[best] < TICKS) { // sécurité (cap 20)
        q_target[best]++;
        leftover--;
      }
      rem[best] = 0; // consommé
    }
  }

  // Étape 3 : reset période
  for (uint8_t i = 0; i < ZONES; ++i) {
    q_used[i] = 0;
    acc[i]    = 0;
  }
  tickIndex = 0;
}

// Sélectionne jusqu'à CAPACITY zones à allumer pour la tranche courante
void selectWinnersForThisSlice(bool onOut[ZONES]) {
  // Incrément des accumulateurs (lisse la répartition)
  for (uint8_t i = 0; i < ZONES; ++i) {
    acc[i] += q_target[i];
    onOut[i] = false;
  }

  // On choisit au plus 2 zones avec acc le plus grand
  bool chosen[ZONES] = {false,false,false,false};
  for (uint8_t k = 0; k < CAPACITY; ++k) {
    int16_t bestVal = INT16_MIN;
    int8_t  bestIdx = -1;

    for (uint8_t i = 0; i < ZONES; ++i) {
      if (chosen[i]) continue;             // déjà prise ce tick
      if (q_used[i] >= q_target[i]) continue; // quota atteint
      if (acc[i] > bestVal) { bestVal = acc[i]; bestIdx = i; }
    }
    if (bestIdx < 0) break; // rien à attribuer

    // Attribue un slot à bestIdx
    onOut[bestIdx] = true;
    chosen[bestIdx] = true;
    q_used[bestIdx]++;
    acc[bestIdx] -= (int16_t)TICKS; // 'consomme' une occurrence
  }
}

// Applique les sorties pour cette tranche
void applySliceOutputs(const bool onOut[ZONES]) {
  for (uint8_t i = 0; i < ZONES; ++i) {
    writeOpto(i, onOut[i]);
  }
}

// ===== Arduino =====
void setup() {
  delay( 3000 ); // power-up safety delay

  pinMode(LED_STAT,  OUTPUT);


  debugSerial.begin(9600);
  debugSerial.println("Bonjour, c'est Thermonuino Basse Tention");

  for (uint8_t z = 0; z < ZONES; ++z) {
    pinMode(Z_OPTO_PINS[z], OUTPUT);
    pinMode(Z_LED_PINS[z],  OUTPUT);
    writeOpto(z, false);
  }
  Serial.begin(LINK_BAUD);
  status.blink(800,150);

  sliceStartMs = millis();
  computePlanFromConsignes(); // plan initial (tout à 0)
}

void loop() {
  status.blink();
  uint32_t now = millis();

  // --- Réception d'un Statut ---
  Statut st;
  if (rx.poll(Serial, st)) {
    for (uint8_t i = 0; i < ZONES; ++i) consigne_raw[i] = st.consigne[i];
    lastRxMs = now;
    replanNextSlice = true; // on prendra en compte à la prochaine frontière de 15 s
    status.blink(2500,1000);
  }

  // --- Failsafe (plus de nouvelles) ---
  if (lastRxMs != 0 && (now - lastRxMs) > FAILSAFE_MS) {
    for (uint8_t i = 0; i < ZONES; ++i) consigne_raw[i] = 0;
    replanNextSlice = true;
    lastRxMs = 0;
    status.blink(800,150);
  }

  // --- Passage à la tranche suivante toutes les 15 s ---
  if ((now - sliceStartMs) >= SLICE_MS) {
    // Avance "sans dérive"
    sliceStartMs += SLICE_MS;

    // Nouvelle période si on vient de finir les 20 tranches
    if (++tickIndex >= TICKS) {
      tickIndex = 0;
      replanNextSlice = true; // recalcul périodique même si consignes inchangées
    }

    // Replanifier au début de tranche si demandé
    if (replanNextSlice) {
      computePlanFromConsignes();
      replanNextSlice = false;
    }

    // Choisir les gagnants pour cette tranche et appliquer
    bool onNow[ZONES];
    selectWinnersForThisSlice(onNow);
    applySliceOutputs(onNow);
  }

  // Boucle 'cool'
  delay(5);
}
