/*
  Test PCB Thermonuino - alimentation RF ON/OFF

  But:
    Verifier si la partie RF commandee par RF_EN s'allume et s'eteint bien.
    Mesurer la tension sur l'alimentation du module RF / CC1101 pendant le test.

  D'apres pinout.txt:
    RF_EN   PCINT0  / PB0 / D8
    RF_GDO0 PCINT1  / PB1 / D9
    RF_CSN  PCINT2  / PB2 / D10
    RF_MOSI PCINT3  / PB3 / D11
    RF_MISO PCINT4  / PB4 / D12
    RF_SCK  PCINT5  / PB5 / D13
    LED     PCINT21 / PD5 / D5

  Polarite:
    Regler RF_EN_ON_LEVEL selon le PCB.
    Le fichier est actuellement configure avec RF_EN HIGH = RF ON.

  Codes LED:
    - LED allumee fixe pendant RF ON
    - LED eteinte pendant RF OFF
    - 3 blinks courts avant le test haute impedance

  Sequence:
    1. 10 cycles: RF ON 3 s, puis RF OFF 3 s avec toutes les lignes RF a 0.
    2. RF OFF force pendant 5 s avec toutes les lignes RF a 0.
    3. 3 blinks courts.
    4. RF OFF pendant 10 s avec lignes RF en haute impedance.
    5. Reprise des cycles ON/OFF.

  Si VCC_RF reste haut pendant les phases OFF:
    - verifier la polarite RF_EN_ON_LEVEL
    - verifier pull-up/pull-down de gate
    - verifier orientation/soudure du P-MOS
    - verifier si le CC1101 est alimente par une autre ligne via SPI/protections ESD
*/

const byte PIN_RF_EN = 8;
const byte PIN_RF_GDO0 = 9;
const byte PIN_RF_CSN = 10;
const byte PIN_RF_MOSI = 11;
const byte PIN_RF_MISO = 12;
const byte PIN_RF_SCK = 13;
const byte PIN_LED = 5;

// Regler selon le PCB: HIGH = ON ici, LOW = OFF.
const byte RF_EN_ON_LEVEL = LOW;
const byte RF_EN_OFF_LEVEL = (RF_EN_ON_LEVEL == LOW) ? HIGH : LOW;

const unsigned long RF_ON_MS = 3000;
const unsigned long RF_OFF_MS = 3000;
const unsigned long RF_OFF_FORCED_MS = 5000;
const unsigned long RF_EN_FLOAT_MS = 10000;

void blinkLed(byte count) {
  for (byte i = 0; i < count; i++) {
    digitalWrite(PIN_LED, HIGH);
    delay(100);
    digitalWrite(PIN_LED, LOW);
    delay(150);
  }
}

void rfOn() {
  pinMode(PIN_RF_EN, OUTPUT);
  digitalWrite(PIN_RF_EN, RF_EN_ON_LEVEL);

  delay(20);

  pinMode(PIN_RF_CSN, OUTPUT);
  digitalWrite(PIN_RF_CSN, HIGH);
  pinMode(PIN_RF_MOSI, OUTPUT);
  digitalWrite(PIN_RF_MOSI, LOW);
  pinMode(PIN_RF_SCK, OUTPUT);
  digitalWrite(PIN_RF_SCK, LOW);
  pinMode(PIN_RF_MISO, INPUT);
  pinMode(PIN_RF_GDO0, INPUT);

  digitalWrite(PIN_LED, HIGH);
}

void rfOff() {
  // Important: ne laisser aucune broche MCU haute vers un module RF non alimente.
  digitalWrite(PIN_RF_CSN, LOW);
  pinMode(PIN_RF_CSN, OUTPUT);

  digitalWrite(PIN_RF_MOSI, LOW);
  pinMode(PIN_RF_MOSI, OUTPUT);

  digitalWrite(PIN_RF_SCK, LOW);
  pinMode(PIN_RF_SCK, OUTPUT);

  pinMode(PIN_RF_MISO, INPUT);
  digitalWrite(PIN_RF_MISO, LOW);

  pinMode(PIN_RF_GDO0, INPUT);
  digitalWrite(PIN_RF_GDO0, LOW);

  pinMode(PIN_RF_EN, OUTPUT);
  digitalWrite(PIN_RF_EN, RF_EN_OFF_LEVEL);
  digitalWrite(PIN_LED, LOW);
}

void rfPinsHighImpedanceOff() {
  pinMode(PIN_RF_CSN, INPUT);
  digitalWrite(PIN_RF_CSN, LOW);

  pinMode(PIN_RF_MOSI, INPUT);
  digitalWrite(PIN_RF_MOSI, LOW);

  pinMode(PIN_RF_SCK, INPUT);
  digitalWrite(PIN_RF_SCK, LOW);

  pinMode(PIN_RF_MISO, INPUT);
  digitalWrite(PIN_RF_MISO, LOW);

  pinMode(PIN_RF_GDO0, INPUT);
  digitalWrite(PIN_RF_GDO0, LOW);

  pinMode(PIN_RF_EN, OUTPUT);
  digitalWrite(PIN_RF_EN, RF_EN_OFF_LEVEL);
  pinMode(PIN_RF_EN, INPUT);
  digitalWrite(PIN_LED, LOW);
}

void setup() {
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);

  rfOff();
  delay(1000);
  blinkLed(1);
}

void loop() {
  for (byte i = 0; i < 10; i++) {
    rfOn();
    delay(RF_ON_MS);

    rfOff();
    delay(RF_OFF_MS);
  }

  rfOff();
  delay(RF_OFF_FORCED_MS);

  blinkLed(3);
  rfPinsHighImpedanceOff();
  delay(RF_EN_FLOAT_MS);

  rfOff();
  delay(RF_OFF_MS);
}
