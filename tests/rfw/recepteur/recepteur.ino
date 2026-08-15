/*
  SYN480R 433MHz RX using RadioHead RH_ASK
  RX data pin: D2
*/

#include <RH_ASK.h>
#include <SPI.h>

// speed (bps), rxPin, txPin (unused), pttPin (unused)
RH_ASK driver(2000, 2, 4, 0);

struct Payload {
  uint8_t id;
  uint16_t cnt;
  uint16_t a0;
} __attribute__((packed));

void setup() {
  Serial.begin(9600);
  if (!driver.init()) {
    Serial.println("RH_ASK init failed");
  }
}

void loop() {
  uint8_t buf[RH_ASK_MAX_MESSAGE_LEN];
  uint8_t buflen = sizeof(buf);
  if (driver.recv(buf, &buflen)) {
    if (buflen == sizeof(Payload)) {
      Payload p;
      memcpy(&p, buf, sizeof(Payload));
      Serial.print("ID=");
      Serial.print(p.id);
      Serial.print(" CNT=");
      Serial.print(p.cnt);
      Serial.print(" A0=");
      Serial.println(p.a0);
    } else {
      Serial.print("LEN=");
      Serial.println(buflen);
    }
  }
}
