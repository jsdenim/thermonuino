// RX: compte les fronts sur DATA (D2) et affiche par seconde

static const uint8_t RX_PIN = 2;
volatile uint32_t edgeCount = 0;

void isr_edge() {
  edgeCount++;
}

void setup() {
  Serial.begin(115200);
  pinMode(RX_PIN, INPUT);  // SYN480R drive la sortie
  attachInterrupt(digitalPinToInterrupt(RX_PIN), isr_edge, CHANGE);
  Serial.println(F("RX edge counter ready"));
}

void loop() {
  static uint32_t last = 0;
  if (millis() - last >= 1000) {
    last += 1000;
    noInterrupts();
    uint32_t n = edgeCount;
    edgeCount = 0;
    interrupts();

    Serial.print(F("edges/s="));
    Serial.println(n);
  }
}
