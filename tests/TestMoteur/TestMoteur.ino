// Pins reliés aux L9110S

#define IB1_PIN 14 // PCINT8  (Arduino D14 = A0)
#define IA1_PIN 15 // PCINT9  (Arduino D15 = A1)
#define IB2_PIN 16 // PCINT10 (Arduino D16 = A2)
#define IA2_PIN 17 // PCINT11 (Arduino D17 = A3)


/*
// Séquence demi-pas (8 états pour plus de douceur)
const int stepSequence[8][4] = {
  {1,0,0,0},  // Bobine 1A
  {1,1,0,0},  // 1A + 2A
  {0,1,0,0},  // 2A
  {0,1,1,0},  // 2A + 1B
  {0,0,1,0},  // 1B
  {0,0,1,1},  // 1B + 2B
  {0,0,0,1},  // 2B
  {1,0,0,1}   // 2B + 1A
};
*/

const int stepSequence[4][4] = {
  {1,0,1,0}, // bobine A+, bobine B+
  {0,1,1,0}, // bobine A-, bobine B+
  {0,1,0,1}, // bobine A-, bobine B-
  {1,0,0,1}  // bobine A+, bobine B-
};

int stepIndex = 0;

void setup() {
  pinMode(IB1_PIN, OUTPUT);
  pinMode(IA1_PIN, OUTPUT);
  pinMode(IB2_PIN, OUTPUT);
  pinMode(IA2_PIN, OUTPUT);

  Serial.begin(9600);
  Serial.println("Stepper ready");
}

void loop() {
  // Exemple: tourner 2 tours en avant avec rampe
  stepWithRamp(4380 * 2, true); // 2 tours -> 8760 pas
  delay(1000);
  stepWithRamp(4380 * 2, false); // retour arrière
  delay(2000);
}

void stepWithRamp(long steps, bool forward) {
  int delayMicros = 10000;     // départ lent: 10 ms entre pas
  int targetDelay = 2000;      // vitesse finale: 2 ms entre pas
  int accelSteps = 200;        // nb de pas pour accélérer
  
  for (long i = 0; i < steps; i++) {
    setStep(stepIndex);

    // Avance ou recule
    if (forward) {
      stepIndex = (stepIndex + 1) % 4;
    } else {
      stepIndex = (stepIndex + 7) % 4 ;
    }

    // Ajuste la vitesse (accélération)
    if (i < accelSteps && delayMicros > targetDelay) {
      delayMicros -= (8000 / accelSteps); // rampe descendante
    }

    // Décélération possible en fin de mouvement
    if (i > steps - accelSteps && delayMicros < 10000) {
      delayMicros += (8000 / accelSteps);
    }

    delayMicroseconds(delayMicros);
  }
}

void setStep(int idx) {
  digitalWrite(IB1_PIN, stepSequence[idx][0]);
  digitalWrite(IA1_PIN, stepSequence[idx][1]);
  digitalWrite(IB2_PIN, stepSequence[idx][2]);
  digitalWrite(IA2_PIN, stepSequence[idx][3]);
}