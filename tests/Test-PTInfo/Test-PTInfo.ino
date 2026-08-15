#include <SoftwareSerial.h>

#define TIC_RX 12

// essaie false, puis true si charabia
SoftwareSerial TIC(TIC_RX, 3, false);
// SoftwareSerial TIC(TIC_RX, 3, true); // version inversée

char line[128];
uint8_t pos = 0;

void setup() {
  Serial.begin(115200);
  TIC.begin(9600);

  Serial.println("Lecture TIC Standard 9600 via SoftwareSerial");
}

void loop() {
  while (TIC.available()) {
    uint8_t c = TIC.read();

    // TIC 7E1 lue en 8N1 : on enlève le bit de parité
    c &= 0x7F;
    Serial.print((char)c);
    /*
    if (c == '\r') continue;

    if (c == '\n') {
      line[pos] = 0;
      Serial.println(line);
      pos = 0;
    }
    else if (c >= 32 && c <= 126) {
      if (pos < sizeof(line) - 1) {
        line[pos++] = c;
      }
    }
    else {
      // caractère invalide : reset ligne
      pos = 0;
    }
    */
  }
}