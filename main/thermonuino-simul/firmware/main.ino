#include "../src-core/greetings.h"

void setup() {
  Serial.begin(9600);
  setupThermostat(17.0);
  resetThermostat();
  Serial.println(evaluateThermostatSlot(0, 18.5, 0.0, 1, 0));
}

void loop() {
}
