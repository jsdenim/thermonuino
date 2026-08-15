#include <ELECHOUSE_CC1101_SRC_DRV.h>

const uint8_t PIN_RF_GDO0 = 2;
const uint8_t PIN_RF_GDO2 = 3;
const uint8_t PIN_FLOTTEUR = 4;
const uint8_t PIN_CMD_POMPE = 5;
const uint8_t PIN_CMD_INTERPHONE = 6;
const uint8_t PIN_CMD_ARROSAGE = 7;

const uint8_t BT300_SPI_SELECT_PIN = 10;
const uint8_t BT300_SPI_MOSI_PIN = 11;
const uint8_t BT300_SPI_MISO_PIN = 12;
const uint8_t BT300_SPI_SCK_PIN = 13;

const uint8_t PIN_BOUTON_POMPE = A0;
const uint8_t PIN_BOUTON_ARROSAGE = A1;
const uint8_t PIN_BOUTON_RF = A2;
const uint8_t PIN_BOUTON_INTERPHONE = A3;

const unsigned long DEFAULT_BUTTON_ACTION_MS = 1000;
const unsigned long FLOAT_PUMP_ACTION_MS = 1000;
const unsigned long DEBOUNCE_MS = 50;
const unsigned long FLOAT_HIGH_CONFIRM_MS = 2000;

struct OutputAction {
  const char *name;
  uint8_t pin;
  bool active;
  unsigned long stopAtMs;
};

OutputAction pumpAction = {"POMPE", PIN_CMD_POMPE, false, 0};
OutputAction interphoneAction = {"INTERPHONE", PIN_CMD_INTERPHONE, false, 0};
OutputAction arrosageAction = {"ARROSAGE", PIN_CMD_ARROSAGE, false, 0};

struct ButtonState {
  const char *name;
  uint8_t pin;
  bool stableState;
  bool lastReading;
  unsigned long lastChangeMs;
};

ButtonState pumpButton = {"BOUTON_POMPE", PIN_BOUTON_POMPE, false, false, 0};
ButtonState arrosageButton = {"BOUTON_ARROSAGE", PIN_BOUTON_ARROSAGE, false, false, 0};
ButtonState rfButton = {"BOUTON_RF", PIN_BOUTON_RF, false, false, 0};
ButtonState interphoneButton = {"BOUTON_INTERPHONE", PIN_BOUTON_INTERPHONE, false, false, 0};

bool cc1101Ready = false;
bool lastFloatState = false;
bool lastRawFloatState = false;
bool floatPumpTriggered = false;
bool floatHighPending = false;
unsigned long floatHighSinceMs = 0;
String serialBuffer;

void printHelp() {
  Serial.println(F("Commandes serie disponibles :"));
  Serial.println(F("  I=5  -> active l'interphone pendant 5 secondes"));
  Serial.println(F("  P=5  -> active la pompe pendant 5 secondes"));
  Serial.println(F("  A=5  -> active l'arrosage pendant 5 secondes"));
  Serial.println(F("  R    -> teste le CC1101"));
  Serial.println(F("  H    -> affiche cette aide"));
}

bool initCC1101() {
  if (!ELECHOUSE_cc1101.getCC1101()) {
    return false;
  }

  ELECHOUSE_cc1101.setGDO(PIN_RF_GDO0, PIN_RF_GDO2);
  ELECHOUSE_cc1101.Init();
  ELECHOUSE_cc1101.setMHZ(433.92);
  ELECHOUSE_cc1101.setPA(10);
  return true;
}

void setOutputIdle(OutputAction &action) {
  digitalWrite(action.pin, LOW);
  action.active = false;
  action.stopAtMs = 0;
}

void startOutputAction(OutputAction &action, unsigned long durationMs, const char *source) {
  digitalWrite(action.pin, HIGH);
  action.active = true;
  action.stopAtMs = millis() + durationMs;

  Serial.print(F("[ACTION] "));
  Serial.print(action.name);
  Serial.print(F(" ON via "));
  Serial.print(source);
  Serial.print(F(" pour "));
  Serial.print(durationMs);
  Serial.println(F(" ms"));
}

void updateOutputAction(OutputAction &action, unsigned long nowMs) {
  if (!action.active) {
    return;
  }

  if ((long)(nowMs - action.stopAtMs) >= 0) {
    setOutputIdle(action);
    Serial.print(F("[ACTION] "));
    Serial.print(action.name);
    Serial.println(F(" OFF"));
  }
}

void testRF(const char *source) {
  Serial.print(F("[RF] Test demande par "));
  Serial.println(source);

  cc1101Ready = initCC1101();

  if (cc1101Ready) {
    Serial.println(F("[RF] CC1101 detecte et initialise : OK"));
  } else {
    Serial.println(F("[RF] CC1101 non detecte ou init impossible : KO"));
  }
}

void handleButtonPress(ButtonState &button) {
  Serial.print(F("[ENTREE] "));
  Serial.print(button.name);
  Serial.println(F(" appuye"));

  if (button.pin == PIN_BOUTON_POMPE) {
    startOutputAction(pumpAction, DEFAULT_BUTTON_ACTION_MS, "bouton pompe");
  } else if (button.pin == PIN_BOUTON_ARROSAGE) {
    startOutputAction(arrosageAction, DEFAULT_BUTTON_ACTION_MS, "bouton arrosage");
  } else if (button.pin == PIN_BOUTON_INTERPHONE) {
    startOutputAction(interphoneAction, DEFAULT_BUTTON_ACTION_MS, "bouton interphone");
  } else if (button.pin == PIN_BOUTON_RF) {
    testRF("bouton RF");
  }
}

void updateButton(ButtonState &button, unsigned long nowMs) {
  bool reading = digitalRead(button.pin) == HIGH;

  if (reading != button.lastReading) {
    button.lastChangeMs = nowMs;
    button.lastReading = reading;
  }

  if ((nowMs - button.lastChangeMs) < DEBOUNCE_MS) {
    return;
  }

  if (reading != button.stableState) {
    button.stableState = reading;

    if (button.stableState) {
      handleButtonPress(button);
    }
  }
}

void handleFloat(unsigned long nowMs) {
  bool rawFloatState = digitalRead(PIN_FLOTTEUR) == HIGH;

  if (rawFloatState != lastRawFloatState) {
    lastRawFloatState = rawFloatState;
  }

  if (rawFloatState) {
    if (!floatHighPending && !lastFloatState) {
      floatHighPending = true;
      floatHighSinceMs = nowMs;
    }

    if (floatHighPending && !lastFloatState && (nowMs - floatHighSinceMs) >= FLOAT_HIGH_CONFIRM_MS) {
      lastFloatState = true;
      floatHighPending = false;

      Serial.println(F("[ENTREE] FLOTTEUR=HIGH confirme (eau detectee)"));
    }
  } else {
    if (floatHighPending) {
      floatHighPending = false;
    }

    if (lastFloatState) {
      lastFloatState = false;
      Serial.println(F("[ENTREE] FLOTTEUR=LOW (pas d'eau)"));
    }
  }

  if (lastFloatState && !floatPumpTriggered) {
    floatPumpTriggered = true;
    startOutputAction(pumpAction, FLOAT_PUMP_ACTION_MS, "flotteur");
  }

  if (!lastFloatState) {
    floatPumpTriggered = false;
  }
}

void handleCommand(String command) {
  command.trim();
  command.toUpperCase();

  if (command.length() == 0) {
    return;
  }

  Serial.print(F("[SERIE] Recu: "));
  Serial.println(command);

  if (command == "H" || command == "HELP") {
    printHelp();
    return;
  }

  if (command == "R") {
    testRF("commande serie");
    return;
  }

  int equalPos = command.indexOf('=');
  if (equalPos <= 0 || equalPos == (command.length() - 1)) {
    Serial.println(F("[SERIE] Format invalide. Exemple: I=5"));
    return;
  }

  char actionCode = command.charAt(0);
  long seconds = command.substring(equalPos + 1).toInt();

  if (seconds <= 0) {
    Serial.println(F("[SERIE] Duree invalide."));
    return;
  }

  unsigned long durationMs = (unsigned long)seconds * 1000UL;

  switch (actionCode) {
    case 'I':
      startOutputAction(interphoneAction, durationMs, "serie");
      break;
    case 'P':
      startOutputAction(pumpAction, durationMs, "serie");
      break;
    case 'A':
      startOutputAction(arrosageAction, durationMs, "serie");
      break;
    default:
      Serial.println(F("[SERIE] Commande inconnue. Utiliser I, P, A ou R."));
      break;
  }
}

void handleSerial() {
  while (Serial.available() > 0) {
    char incoming = (char)Serial.read();

    if (incoming == '\r') {
      continue;
    }

    if (incoming == '\n') {
      handleCommand(serialBuffer);
      serialBuffer = "";
      continue;
    }

    serialBuffer += incoming;
  }
}

void setup() {
  Serial.begin(9600);
  delay(500);

  pinMode(PIN_FLOTTEUR, INPUT);
  pinMode(PIN_BOUTON_POMPE, INPUT);
  pinMode(PIN_BOUTON_ARROSAGE, INPUT);
  pinMode(PIN_BOUTON_RF, INPUT);
  pinMode(PIN_BOUTON_INTERPHONE, INPUT);

  pinMode(PIN_CMD_POMPE, OUTPUT);
  pinMode(PIN_CMD_INTERPHONE, OUTPUT);
  pinMode(PIN_CMD_ARROSAGE, OUTPUT);

  setOutputIdle(pumpAction);
  setOutputIdle(interphoneAction);
  setOutputIdle(arrosageAction);

  pinMode(BT300_SPI_SELECT_PIN, OUTPUT);
  pinMode(BT300_SPI_MOSI_PIN, OUTPUT);
  pinMode(BT300_SPI_MISO_PIN, INPUT);
  pinMode(BT300_SPI_SCK_PIN, OUTPUT);
  pinMode(PIN_RF_GDO0, INPUT);
  pinMode(PIN_RF_GDO2, INPUT);

  lastFloatState = false;
  lastRawFloatState = digitalRead(PIN_FLOTTEUR) == HIGH;
  pumpButton.stableState = digitalRead(PIN_BOUTON_POMPE) == HIGH;
  pumpButton.lastReading = pumpButton.stableState;
  arrosageButton.stableState = digitalRead(PIN_BOUTON_ARROSAGE) == HIGH;
  arrosageButton.lastReading = arrosageButton.stableState;
  rfButton.stableState = digitalRead(PIN_BOUTON_RF) == HIGH;
  rfButton.lastReading = rfButton.stableState;
  interphoneButton.stableState = digitalRead(PIN_BOUTON_INTERPHONE) == HIGH;
  interphoneButton.lastReading = interphoneButton.stableState;

  Serial.println(F("=== TEST PCB BT300 ==="));
  Serial.println(F("Boutons: A0=POMPE, A1=ARROSAGE, A2=RF, A3=INTERPHONE"));
  Serial.println(F("Entree flotteur: D4"));
  Serial.println(F("Sorties: D5=POMPE, D6=INTERPHONE, D7=ARROSAGE"));
  Serial.print(F("[ENTREE] FLOTTEUR brut initial="));
  Serial.println(lastRawFloatState ? F("HIGH") : F("LOW"));
  Serial.println(F("[ENTREE] Validation flotteur: HIGH stable > 2000 ms"));

  cc1101Ready = initCC1101();
  Serial.println(cc1101Ready ? F("[RF] CC1101 detecte au demarrage") : F("[RF] CC1101 absent au demarrage"));

  printHelp();
  Serial.println(F("Systeme pret."));
}

void loop() {
  unsigned long nowMs = millis();

  handleSerial();
  updateButton(pumpButton, nowMs);
  updateButton(arrosageButton, nowMs);
  updateButton(rfButton, nowMs);
  updateButton(interphoneButton, nowMs);
  handleFloat(nowMs);

  updateOutputAction(pumpAction, nowMs);
  updateOutputAction(interphoneAction, nowMs);
  updateOutputAction(arrosageAction, nowMs);
}
