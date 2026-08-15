#include <Arduino.h>
#include <Wire.h>
#include <EEPROM.h>
#include <avr/sleep.h>
#include <avr/wdt.h>
#include <avr/power.h>
#include "frame.h"

// ================== CONFIG ==================
static const uint8_t DEV_ID_COMPILED = 7;   // <-- TU CHANGES ICI avant chaque upload
static const uint16_t EEPROM_MAGIC = 0xBEEF;
static const int EEPROM_MAGIC_ADDR = 0;     // uint16_t
static const int EEPROM_DEVID_ADDR = 2;     // uint8_t

// Pins
static const uint8_t PIN_RFOUT    = 4;   // PD4 -> DIN WL4456 via R59
static const uint8_t PIN_RFEN     = 5;   // PD5 -> enable alim RF_3V3
static const uint8_t PIN_TEMPIGN = 6;   // PD6 (jumper vers GND => ignore)
static const uint8_t PIN_BOOSTBTN = 7;   // PD7 (pullup)
static const uint8_t PIN_DOOR     = 10;  // PB2 (pullup) reed: LOW => open
static const uint8_t PIN_ADDR0    = A2;  // PC2
static const uint8_t PIN_ADDR1    = A3;  // PC3
static const uint8_t PIN_BATSENSE = A1;  // PC1 ADC1

// Battery divider: R32=1M, R33=1M => Vadc = Vbat/2
static const float ADC_REF_V = 3.3f;
static const float DIV_RATIO = 0.5f;

// RF Manchester
static const uint16_t HALF_BIT_US = 200;     // 200us => robuste (à ajuster si besoin)
static const uint8_t RF_REPEATS = 6;

// Sync
static const uint8_t SYNC1 = 0xA5;
static const uint8_t SYNC2 = 0x5A;
static const uint8_t PROTO_VER = 0x01;

// ================== CRC8 Dallas/Maxim ==================
static uint8_t crc8_maxim(const uint8_t* data, size_t len) {
  uint8_t crc = 0x00;
  for (size_t i = 0; i < len; ++i) {
    uint8_t inbyte = data[i];
    for (uint8_t j = 0; j < 8; j++) {
      uint8_t mix = (crc ^ inbyte) & 0x01;
      crc >>= 1;
      if (mix) crc ^= 0x8C;
      inbyte >>= 1;
    }
  }
  return crc;
}



// ================== State ==================
static uint8_t g_devId = 0xFF;
static uint8_t g_seq = 0;

static bool doorLearned = false;
static bool lastDoorOpen = false;

static uint8_t doorCount = 0;
static uint8_t motionCount = 0;
static uint8_t boostReqCount = 0;

// ================== Utils ==================
static uint8_t readZone() {
  pinMode(PIN_ADDR0, INPUT_PULLUP);
  pinMode(PIN_ADDR1, INPUT_PULLUP);
  uint8_t b0 = (digitalRead(PIN_ADDR0) == LOW) ? 1 : 0;
  uint8_t b1 = (digitalRead(PIN_ADDR1) == LOW) ? 2 : 0;
  return (uint8_t)(1 + b0 + b1);
}

static bool tempIgnored() {
  pinMode(PIN_TEMPIGN, INPUT_PULLUP);
  return (digitalRead(PIN_TEMPIGN) == LOW);
}

static bool readDoorOpen() {
  // reed: LOW => open
  return (digitalRead(PIN_DOOR) == LOW);
}

static uint16_t readBattery_mV() {
  uint32_t acc = 0;
  for (int i=0;i<8;i++){
    acc += analogRead(PIN_BATSENSE);
    delay(2);
  }
  float adc = acc / 8.0f;
  float vadc = (adc / 1023.0f) * ADC_REF_V;
  float vbat = vadc / DIV_RATIO; // here => *2
  return (uint16_t)(vbat * 1000.0f + 0.5f);
}

// (Option) si tu veux I2C HDC1080 ici, on le réintégrera — je laisse juste l’API:
static bool readTempHum(float &tC, float &hPct) {
  // TODO: remettre ton code HDC1080 ici
  // Pour l’instant, valeurs dummy
  tC = 20.0f;
  hPct = 50.0f;
  return true;
}

// ================== EEPROM init (TA CONTRAINTE) ==================
static void initOrReadDevId() {
  uint16_t magic = 0;
  EEPROM.get(EEPROM_MAGIC_ADDR, magic);

  if (magic != EEPROM_MAGIC) {
    // EEPROM pas initialisée : on écrit une seule fois
    magic = EEPROM_MAGIC;
    EEPROM.put(EEPROM_MAGIC_ADDR, magic);
    EEPROM.update(EEPROM_DEVID_ADDR, DEV_ID_COMPILED);
  }

  // Dans tous les cas, on lit la valeur définitive depuis EEPROM
  g_devId = EEPROM.read(EEPROM_DEVID_ADDR);
}

// ================== Manchester TX ==================
static inline void rfHalf(bool level) {
  digitalWrite(PIN_RFOUT, level ? HIGH : LOW);
  delayMicroseconds(HALF_BIT_US);
}

// bit 0 -> 01, bit 1 -> 10
static void rfSendManchesterBit(bool b) {
  if (!b) { rfHalf(false); rfHalf(true); }
  else    { rfHalf(true);  rfHalf(false); }
}

static void rfSendManchesterByte(uint8_t v) {
  for (int i=7; i>=0; --i) {
    rfSendManchesterBit((v >> i) & 1);
  }
}

static void rfSendPreamble(uint8_t bits = 40) {
  // en Manchester, envoyer une alternance est parfait pour calage
  for (uint8_t i=0; i<bits; i++) {
    rfSendManchesterBit(i & 1);
  }
}

static void rfSendFrame(const Frame &f) {
  Frame tmp = f;
  tmp.crc = crc8_maxim((uint8_t*)&tmp, sizeof(Frame)-1);

  // RF ON (P-MOS => gate LOW)
  digitalWrite(PIN_RFEN, LOW);
  delay(800);

  for (uint8_t r = 0; r < RF_REPEATS; r++) {
    rfSendPreamble(40);
    rfSendManchesterByte(SYNC1);
    rfSendManchesterByte(SYNC2);

    const uint8_t* p = (const uint8_t*)&tmp;
    for (size_t i = 0; i < sizeof(Frame); i++) {
      rfSendManchesterByte(p[i]);
    }

    
    delay(6 + (r * 3));
  }

  digitalWrite(PIN_RFOUT, LOW);
  digitalWrite(PIN_RFEN, HIGH);   // RF OFF
}

// ================== App logic ==================
static void buildAndSend(bool forceSend = false) {
  float tC=0, h=0;
  bool ign = tempIgnored();
  if (!ign) (void)readTempHum(tC, h);

  Frame f{};
  f.ver = PROTO_VER;
  f.seq = g_seq++;
  f.dev_id = g_devId;
  f.zone = readZone();

  if (ign) {
    f.temp_c_x100 = -4500;
    f.hum_x100 = 0;
  } else {
    f.temp_c_x100 = (int16_t)lroundf(tC * 100.0f);
    f.hum_x100 = (uint16_t)lroundf(h * 100.0f);
  }

  f.bat_mv = readBattery_mV();

  // Door logic
  bool doorNow = readDoorOpen();
  if (!doorLearned && doorNow) doorLearned = true;
  if (doorLearned && (doorNow != lastDoorOpen)) {
    if (doorCount != 255) doorCount++;
    lastDoorOpen = doorNow;
  }

  f.door_open = doorNow ? 1 : 0;
  f.door_learned = doorLearned ? 1 : 0;
  f.door_count = doorCount;
  f.motion_count = motionCount;
  f.boost_req_count = boostReqCount;

  rfSendFrameNRZ(f);

  // reset "since last send" counters
  doorCount = 0;
  motionCount = 0;
  boostReqCount = 0;
}

void setup() {
  pinMode(PIN_RFEN, OUTPUT);
  digitalWrite(PIN_RFEN, HIGH); 
  digitalWrite(PIN_RFOUT, LOW);
  digitalWrite(PIN_RFEN, LOW);

  pinMode(PIN_DOOR, INPUT_PULLUP);
  pinMode(PIN_BOOSTBTN, INPUT_PULLUP);

  initOrReadDevId();

  lastDoorOpen = readDoorOpen();

  // Boot burst minimal (exemple)
  for (uint8_t i=0;i<6;i++){
    buildAndSend(true);
    delay(500);
  }
}

void loop() {
  // Exemple minimal: si bouton boost pressé -> incrémente + envoie immédiat
  static bool lastBtn = HIGH;
  bool now = digitalRead(PIN_BOOSTBTN);
  if (lastBtn == HIGH && now == LOW) {
    delay(15);
    if (digitalRead(PIN_BOOSTBTN) == LOW) {
      if (boostReqCount != 255) boostReqCount++;
      buildAndSend(true);
      while (digitalRead(PIN_BOOSTBTN) == LOW) delay(5); // attend relâche
    }
  }
  lastBtn = now;

  // TODO: intégrer sleep + WDT + INT (on le remettra comme dans ta V1),
  // ici on reste volontairement simple pour valider la trame + la radio.
  delay(50);
}


// ===== NRZ OOK TX (WL4456 via DIN) =====
static const uint16_t BIT_US = 1000;     // 1000 bps (robuste SYN480R)
static const uint8_t  PREAMBLE_BYTES = 12;

#define RF_ON()   digitalWrite(PIN_RFEN, LOW)   // P-MOS: gate LOW = ON
#define RF_OFF()  digitalWrite(PIN_RFEN, HIGH)  // gate HIGH = OFF

static inline void rfBit(bool one) {
  digitalWrite(PIN_RFOUT, one ? HIGH : LOW);
  delayMicroseconds(BIT_US);
}

static void rfSendByteNRZ(uint8_t v) {
  for (int i = 7; i >= 0; --i) {
    rfBit((v >> i) & 1);
  }
}

static void rfSendFrameNRZ(const Frame &f) {
  Frame tmp = f;
  tmp.crc = 0;
  tmp.crc = crc8_maxim((uint8_t*)&tmp, sizeof(Frame) - 1);

  RF_ON();
  delay(5); // stabilisation alim RF_3V3

  for (uint8_t r = 0; r < RF_REPEATS; r++) {
    // Préambule 0xAA répété (10101010...) -> cale AGC + seuil SYN480
    for (uint8_t i = 0; i < PREAMBLE_BYTES; i++) rfSendByteNRZ(0xAA);

    // SYNC
    rfSendByteNRZ(0xA5);
    rfSendByteNRZ(0x5A);

    // Payload fixe (16 octets)
    const uint8_t* p = (const uint8_t*)&tmp;
    for (size_t i = 0; i < sizeof(Frame); i++) rfSendByteNRZ(p[i]);

    // Silence inter-répétition + petit jitter
    digitalWrite(PIN_RFOUT, LOW);
    delay(8 + (r * 3));
  }

  digitalWrite(PIN_RFOUT, LOW);
  RF_OFF();
}

