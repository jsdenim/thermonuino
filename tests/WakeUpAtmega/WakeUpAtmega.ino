/*
  Clock de secours pour ATmega328P
  Sortie : OC1A (D9 sur Arduino UNO/Nano)
  Fréquence : 1 MHz
*/

void setup() {
  pinMode(9, OUTPUT);

  // Timer1 en mode CTC, toggle OC1A
  TCCR1A = _BV(COM1A0);          // Toggle OC1A on compare match
  TCCR1B = _BV(WGM12) | _BV(CS10); // CTC, prescaler = 1
  OCR1A  = 7;                    // 16 MHz / (2*(7+1)) = 1 MHz
}

void loop() {
  // rien à faire, la clock est matérielle
}
