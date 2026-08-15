// Minimal target sketch for UART-over-MOSI/MISO relay test
#include <SoftwareSerial.h>

#define RELAY_RX_PIN MOSI
#define RELAY_TX_PIN MISO
#define RELAY_BAUDRATE 9600

SoftwareSerial relaySerial(RELAY_RX_PIN, RELAY_TX_PIN);

void setup() {
  relaySerial.begin(RELAY_BAUDRATE);
  relaySerial.println("target ready");
}

void loop() {
  if (relaySerial.available()) {
    int b = relaySerial.read();
    relaySerial.write(b); // echo
  }
}
