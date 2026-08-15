#include <SPI.h>

#define CC1101_CS   10

// Registres CC1101
#define CC1101_PARTNUM   0x30
#define CC1101_VERSION   0x31

// Bit lecture + burst/status
#define READ_SINGLE      0x80
#define READ_BURST       0xC0

byte cc1101ReadReg(byte addr) {
  digitalWrite(CC1101_CS, LOW);
  delayMicroseconds(10);

  SPI.transfer(addr | READ_SINGLE);
  byte value = SPI.transfer(0x00);

  digitalWrite(CC1101_CS, HIGH);
  return value;
}

byte cc1101ReadStatus(byte addr) {
  digitalWrite(CC1101_CS, LOW);
  delayMicroseconds(10);

  SPI.transfer(addr | READ_BURST);
  byte value = SPI.transfer(0x00);

  digitalWrite(CC1101_CS, HIGH);
  return value;
}

void cc1101Strobe(byte command) {
  digitalWrite(CC1101_CS, LOW);
  delayMicroseconds(10);

  SPI.transfer(command);

  digitalWrite(CC1101_CS, HIGH);
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("Test communication SPI avec module CC1101 RF433");
  Serial.println("Brochage:");
  Serial.println("GND  -> GND");
  Serial.println("3.3V -> VCC");
  Serial.println("10   -> CSN/SS");
  Serial.println("11   -> SI/MOSI");
  Serial.println("12   -> SO/MISO");
  Serial.println("13   -> SCK");
  Serial.println();

  pinMode(CC1101_CS, OUTPUT);
  digitalWrite(CC1101_CS, HIGH);

  SPI.begin();
  SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));

  Serial.println("Reset du CC1101...");
  cc1101Strobe(0x30); // SRES
  delay(10);

  Serial.println("Demarrage des tentatives de lecture...");
}

void loop() {
  static int tentative = 1;

  Serial.print("Tentative ");
  Serial.print(tentative);
  Serial.println(" :");

  byte partnum = cc1101ReadStatus(CC1101_PARTNUM);
  byte version = cc1101ReadStatus(CC1101_VERSION);

  Serial.print("  PARTNUM = 0x");
  if (partnum < 0x10) Serial.print("0");
  Serial.println(partnum, HEX);

  Serial.print("  VERSION = 0x");
  if (version < 0x10) Serial.print("0");
  Serial.println(version, HEX);

  if (partnum == 0x00 && version != 0x00 && version != 0xFF) {
    Serial.println("  OK: le CC1101 repond sur le bus SPI.");
  } else if (partnum == 0xFF || version == 0xFF) {
    Serial.println("  ERREUR: lecture a 0xFF, verifier MISO/CSN/alimentation.");
  } else if (partnum == 0x00 && version == 0x00) {
    Serial.println("  ERREUR: lecture a 0x00, verifier cablage SPI ou module non alimente.");
  } else {
    Serial.println("  Reponse recue, mais valeur inattendue.");
  }

  Serial.println();

  tentative++;
  delay(1000);
}