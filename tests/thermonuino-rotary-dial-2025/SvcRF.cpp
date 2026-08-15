#include "SvcRF.h"
#include "Logging.h"

// --- Alecto WS1700 timings (µs) ---
#define LP_MIN 1100
#define LP_MAX 1500
#define SP_MIN 300
#define SP_MAX 700
#define ASP_MIN 1600
#define ASP_MAX 2200
#define ALP_MIN 3200
#define ALP_MAX 4400
#define AEP_MIN 8600
#define AEP_MAX 11000

// Anti-spam: ignore les répétitions pendant 500 ms (au lieu de delay(500))
static uint32_t s_ignoreUntilMs = 0;

// pulseIn avec timeout borne (wrap-safe, max ~11 ms)
static inline unsigned long pulseHigh(uint32_t to_us) { return pulseIn(PIN_RF_RX, HIGH, to_us); }
static inline unsigned long pulseLow (uint32_t to_us) { return pulseIn(PIN_RF_RX, LOW,  to_us); }

// Décode UNE trame complète Alecto WS1700 (version compacte, bloquante max ~<150 ms)
// Retourne true si {t0.1°C, h%, channel, lowBatt} valides.
static bool alecto_read(int &t_deciC, int &h_pct, uint8_t &channel, bool &lowBatt) {
  // 1) Chercher le "end low" (~9.2 ms) — jusqu'à 100 essais
  for (int p=0; p<100; ++p) {
    unsigned long dur = pulseLow(AEP_MAX);
    if (dur==0) return false;                      // timeout
    if (dur > AEP_MIN && dur < AEP_MAX) break;     // trouvé
    if (p==99) return false;
  }

  // 2) Lire l'entête (4 lows) → v doit valoir 187 (0b10111011)
  uint8_t v = 0;
  for (int i=0; i<4; ++i) {
    v <<= 2;
    unsigned long d = pulseLow(ALP_MAX);
    if      (d>ASP_MIN && d<ASP_MAX) v |= 2;       // "short low"
    else if (d>ALP_MIN && d<ALP_MAX) v |= 3;       // "long low"
    else return false;
  }
  if (v != 187) return false;

  // 3) Lire 32 bits (lows)
  uint8_t bits[32];
  for (int i=0; i<32; ++i) {
    unsigned long d = pulseLow(ALP_MAX);
    if      (d>ASP_MIN && d<ASP_MAX) bits[i] = 0;
    else if (d>ALP_MIN && d<ALP_MAX) bits[i] = 1;
    else return false;
  }

  // 4) End low final
  unsigned long tail = pulseLow(AEP_MAX);
  if (!(tail > AEP_MIN && tail < AEP_MAX)) return false;

  // 5) Décodage champs
  int h=0, t=0;
  for (int i=12; i<=23; ++i) { t = (t<<1) | bits[i]; }
  for (int i=24; i<=31; ++i) { h = (h<<1) | bits[i]; }
  if (h==0) return false;

  if (t > 3840) t -= 4096;  // deux compléments → valeur signée en 0.1°C

  channel = (bits[10] << 1) | bits[11];   // 00:CH1, 01:CH2, 10:CH3, 11:err
  if (channel == 3) return false;

  lowBatt = !bits[8]; // bit 8 = 1 OK, 0 = batterie faible
  t_deciC = t;
  h_pct   = h;
  return true;
}

static bool s_enabled = true;

// Pour éviter les logs spammy, on garde l’état précédent
static bool s_prevOk[3] = {false,false,false};
static bool s_prevLow[3]= {false,false,false};

void SvcRF_init() {
  s_enabled = true;
  // Si tu as besoin d’un pinMode pour le RX 433 :
  pinMode(PIN_RF_RX, INPUT);
  info(F("[RF] init"));
}

void SvcRF_enable(bool on) {
  if (s_enabled == on) return;
  s_enabled = on;
  info(String("[RF] ")+(on?"EN":"DIS"));
}

void SvcRF_onReading(uint8_t slot, int16_t Tq, bool lowBatt) {
  if (!s_enabled) return;                 // on ignore si RF coupée
  if (slot > 2) return;                   // RF1..RF3 seulement

  g_rf[slot].ok       = true;
  g_rf[slot].lowBatt  = lowBatt;
  g_rf[slot].Tq       = Tq;
  g_rf[slot].lastMs   = millis();

  // Logs déclenchés uniquement sur transition / changement notable
  if (!s_prevOk[slot]) {
    info(String("[RF] slot")+String(slot+1)+F(" first frame"));
  }
  if (s_prevLow[slot] != lowBatt) {
    info(String("[RF] slot")+String(slot+1)+F(" batt=")+(lowBatt?"LOW":"OK"));
  }
  s_prevOk[slot]  = true;
  s_prevLow[slot] = lowBatt;

  // Debug “lecture”
  debug(String("[RF] slot")+String(slot+1)+F(" Tq=")+String(Tq)+F(" (")
                   + String(Tq/4.0,2) + F("°C)"));
}

void SvcRF_poll() {
  if (!s_enabled) return;

  // Anti-spam (capteur répète la trame) — on n'ignore que la RÉPÉTITION,
  // pas les timeouts ni les pertes de capteurs.
  if ((int32_t)(millis() - s_ignoreUntilMs) < 0) {
    // Même pendant l'ignore, on continue à surveiller les timeouts
    for (uint8_t i=0; i<3; i++) {
      if (s_prevOk[i] && (millis() - g_rf[i].lastMs) > RF_TIMEOUT_MS) {
        g_rf[i].ok = false; s_prevOk[i] = false;
        info(String("[RF] slot")+String(i+1)+F(" LOST → fallback board"));
      }
    }
    return;
  }

  // --- Détection d'un préambule "≥ 8 short HIGH" (rapide, timeouts bornés) ---
  int shortCount = 0;
  for (int i=0; i<16; ++i) {
    unsigned long d = pulseHigh(LP_MAX);          // borne à ~1.5 ms
    if (d==0) break;                              // pas de signal maintenant
    if (d>SP_MIN && d<SP_MAX) {
      if (++shortCount >= 8) break;               // préambule détecté
    } else {
      shortCount = 0;
    }
  }
  if (shortCount < 8) {
    // Même si on n'a rien décodé, on gère les timeouts RF
    for (uint8_t i=0; i<3; i++) {
      if (s_prevOk[i] && (millis() - g_rf[i].lastMs) > RF_TIMEOUT_MS) {
        g_rf[i].ok = false; s_prevOk[i] = false;
        info(String("[RF] slot")+String(i+1)+F(" LOST → fallback board"));
      }
    }
    return;
  }

  // --- On tente un décodage complet d'une trame ---
  int t_deciC=0, h=0; uint8_t ch=0; bool lowBatt=false;
  if (alecto_read(t_deciC, h, ch, lowBatt)) {
    // map channel (0..2) → slot (0..2)
    uint8_t slot = ch; // CH1→0, CH2→1, CH3→2
    // convertir 0.1°C → quarts de degré (0,25°C) : Tq = round(t * 0.4)
    int16_t Tq = (int16_t) lroundf((float)t_deciC * 0.4f);
    SvcRF_onReading(slot, Tq, lowBatt);

    // ignorer les répétitions du capteur pendant 500 ms (sans bloquer)
    s_ignoreUntilMs = millis() + 500UL;
  }
  // sinon: trame invalide → on laisse retenter au prochain poll()
}
