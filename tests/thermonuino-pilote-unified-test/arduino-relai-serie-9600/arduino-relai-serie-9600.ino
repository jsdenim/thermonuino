/*
  Relai serie 9600 bauds pour tester la carte Thermonuino.

  A televerser sur un Arduino Uno/Nano utilise comme passerelle USB <-> TTL.

  Cablage cote Arduino relais:
    D10 = RX logiciel, a connecter au TX de la carte a tester
    D11 = TX logiciel, a connecter au RX de la carte a tester
    GND = GND commun avec la carte a tester

  Ouvrir le Moniteur Serie a 9600 bauds.
  Les commandes envoyees depuis le PC sont relayees vers la carte a tester.
  Les reponses de la carte a tester sont renvoyees vers le PC.
*/

#include <SoftwareSerial.h>

const uint8_t PIN_DUT_RX = 10;  // Recoit le TX de la carte a tester
const uint8_t PIN_DUT_TX = 11;  // Envoie vers le RX de la carte a tester
const unsigned long BAUD_RATE = 9600;

SoftwareSerial dutSerial(PIN_DUT_RX, PIN_DUT_TX);

void setup() {
  Serial.begin(BAUD_RATE);
  dutSerial.begin(BAUD_RATE);

  Serial.println(F("Relai serie 9600 pret"));
  Serial.println(F("Commandes utiles: AUTO, DIAG, Z1, Z2, Z3, Z4, OFF"));
}

void loop() {
  while (Serial.available() > 0) {
    dutSerial.write(Serial.read());
  }

  while (dutSerial.available() > 0) {
    Serial.write(dutSerial.read());
  }
}
