/*
  SYN115 433MHz TX using RadioHead RH_ASK
  TX data pin: D4
*/

#include <RH_ASK.h>
#include <SPI.h>

// speed (bps), rxPin (unused), txPin, pttPin (unused)
RH_ASK driver(2000, 2, 4, 0);

struct Payload {
  uint8_t id;
  uint16_t cnt;
  uint16_t a0;
} __attribute__((packed));

uint16_t counter = 0;
static const uint8_t PIN_RFEN     = 5;   // PD5 -> enable alim RF_3V3

void setup() {
  Serial.begin(9600);
  if (!driver.init()) {
    Serial.println("RH_ASK init failed");
  }

  pinMode(PIN_RFEN, OUTPUT);
  digitalWrite(PIN_RFEN, LOW);
  delay(1000);
  digitalWrite(PIN_RFEN, HIGH);
  delay(1000);
  digitalWrite(PIN_RFEN, LOW);
}

void loop() {
  Payload p;
  p.id = 1;
  p.cnt = counter++;
  p.a0 = analogRead(A0);

  driver.send((uint8_t *)&p, sizeof(p));
  driver.waitPacketSent();
  delay(1000);
}
