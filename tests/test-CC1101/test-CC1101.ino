/*
  CC1101 Devboard bring-up + TX/RX loopback test
  Same sketch on both boards.

  Wiring (Arduino UNO / Nano):
    SCK  -> D13
    MISO -> D12
    MOSI -> D11
    CSN  -> D10
    GDO0 -> D2   (recommended, used for fast RX check, but sketch also polls)

  LED_BUILTIN:
    - Fast blink at boot: SPI/CC1101 detected OK
    - Slow blink continuously: running
    - Short double-blink on each received valid packet
*/
#include <SPI.h>
#include <ELECHOUSE_CC1101_SRC_DRV.h>

static const uint8_t PIN_CSN  = 10;
static const uint8_t PIN_GDO0 = 2;

static const float RF_MHZ = 433.92;
static const uint16_t TX_PERIOD_MS = 1000;

static const uint8_t MAGIC0 = 0x54; // 'T'
static const uint8_t MAGIC1 = 0x4E; // 'N'

uint32_t txCounter = 0;
uint32_t lastTxMs = 0;

static void blink(uint8_t n, uint16_t onMs=80, uint16_t offMs=120) {
  for (uint8_t i=0; i<n; i++) {
    digitalWrite(LED_BUILTIN, HIGH);
    delay(onMs);
    digitalWrite(LED_BUILTIN, LOW);
    delay(offMs);
  }
}

static void fatalBlink() {
  while (true) {
    blink(3, 120, 120);
    delay(400);
    blink(3, 120, 120);
    delay(400);
    blink(3, 120, 120);
    delay(1000);
  }
}

static bool cc1101_detect_basic() {
  uint8_t partnum = ELECHOUSE_cc1101.SpiReadReg(0x30); // PARTNUM
  uint8_t version = ELECHOUSE_cc1101.SpiReadReg(0x31); // VERSION

  Serial.print(F("CC1101 PARTNUM=0x"));
  Serial.print(partnum, HEX);
  Serial.print(F(" VERSION=0x"));
  Serial.println(version, HEX);

  if (partnum == 0x00 || partnum == 0xFF) return false;
  if (version == 0x00 || version == 0xFF) return false;
  return true;
}

static void cc1101_config() {
  // setSpiPin(sck, miso, mosi, csn)
  ELECHOUSE_cc1101.setSpiPin(13, 12, 11, PIN_CSN);
  ELECHOUSE_cc1101.Init();
  ELECHOUSE_cc1101.setGDO0(PIN_GDO0);

  ELECHOUSE_cc1101.setMHZ(RF_MHZ);
  ELECHOUSE_cc1101.setModulation(2);  // GFSK (most builds)
  ELECHOUSE_cc1101.setDRate(2.4);
  ELECHOUSE_cc1101.setRxBW(58.0);
  ELECHOUSE_cc1101.setPA(10);

  ELECHOUSE_cc1101.SetRx();
}

static void send_packet() {
  uint8_t buf[2 + 1 + 4 + 1];
  buf[0] = MAGIC0;
  buf[1] = MAGIC1;

  uint8_t senderId = (uint8_t)(analogRead(A0) & 0xFF);
  buf[2] = senderId;

  uint32_t c = txCounter++;
  buf[3] = (uint8_t)(c >> 24);
  buf[4] = (uint8_t)(c >> 16);
  buf[5] = (uint8_t)(c >> 8);
  buf[6] = (uint8_t)(c >> 0);

  uint8_t crc = 0;
  for (uint8_t i = 0; i < 7; i++) crc ^= buf[i];
  buf[7] = crc;

  ELECHOUSE_cc1101.SetTx();
  ELECHOUSE_cc1101.SendData(buf, sizeof(buf));
  ELECHOUSE_cc1101.SetRx();

  Serial.print(F("TX counter="));
  Serial.println(c);
}

static bool try_receive_nonblocking() {
  // In SmartRC lib, CheckRxFifo(int t) expects an int.
  // Use t=1 for non-blocking / minimal wait.
  if (!ELECHOUSE_cc1101.CheckRxFifo(1)) return false;

  uint8_t rxbuf[64] = {0};
  uint8_t len = ELECHOUSE_cc1101.ReceiveData(rxbuf);

  if (len < 8) return false;
  if (rxbuf[0] != MAGIC0 || rxbuf[1] != MAGIC1) return false;

  uint8_t crc = 0;
  for (uint8_t i = 0; i < len - 1; i++) crc ^= rxbuf[i];
  if (crc != rxbuf[len - 1]) return false;

  uint32_t c =
    ((uint32_t)rxbuf[3] << 24) |
    ((uint32_t)rxbuf[4] << 16) |
    ((uint32_t)rxbuf[5] << 8)  |
    ((uint32_t)rxbuf[6]);

  Serial.print(F("RX len="));
  Serial.print(len);
  Serial.print(F(" counter="));
  Serial.print(c);
  Serial.print(F(" senderId=0x"));
  Serial.println(rxbuf[2], HEX);

  return true;
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  Serial.begin(115200);
  delay(300);

  SPI.begin();
  pinMode(PIN_CSN, OUTPUT);
  digitalWrite(PIN_CSN, HIGH);

  cc1101_config();

  if (!cc1101_detect_basic()) {
    Serial.println(F("CC1101 NOT detected (SPI wiring / CS / power / GND?)"));
    fatalBlink();
  }

  Serial.println(F("CC1101 detected OK."));
  blink(6, 60, 60);

  lastTxMs = millis();
}

void loop() {
  // heartbeat
  static uint32_t lastHb = 0;
  if (millis() - lastHb > 2000) {
    lastHb = millis();
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
  }

  // periodic TX
  if (millis() - lastTxMs >= TX_PERIOD_MS) {
    lastTxMs += TX_PERIOD_MS;
    send_packet();
  }

  // RX
  if (try_receive_nonblocking()) {
    blink(2, 40, 60);
  }
}