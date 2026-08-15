#include <SoftwareSerial.h>
#include <Wire.h>

// On mappe MOSI (PB3) comme TX et MISO (PB4) comme RX
// Broches Arduino : D11 = MOSI (TX), D12 = MISO (RX)
SoftwareSerial debugSerial(12, 11); // RX = D12 (MISO), TX = D11 (MOSI)
//L'Atmega328 parle à l'Arduino Uno en 9600bauds, mais l'Arduino uno et l'ordi parlent en 19200 bauds. Ne pas oublier de changer 
//ce réglage dans Serial Monitor, puis faire R pour passer en mode relay.

//// Rotary Encoder
#include <ClickEncoder.h>
#include <TimerOne.h>

ClickEncoder *encoder;
int16_t last, value;
#define ROT_CLK_PIN 8 //PB0
#define ROT_QUAD_PIN 3 //PD3 //1
#define ROT_BUTTON_PIN 10 //PB2 //PCINT2

void timerIsr() {
  encoder->service();
}

bool lastClk, lastQuad, lastBtn;

void setup() {
  Serial.begin(9600);

   debugSerial.begin(9600);
    debugSerial.println("Hello via ISP pins (MISO/MOSI)!");


  // Encoder

    encoder = new ClickEncoder(ROT_CLK_PIN, ROT_QUAD_PIN, ROT_BUTTON_PIN, 1, HIGH);
    Timer1.initialize(1000);
    Timer1.attachInterrupt(timerIsr); 
    
    last = -1;

  debugSerial.println("Encodeur prêt (appui ou rotation détectée).");
}

void loop() {
  //Test Rotary Encoder
  value += encoder->getValue();
  if (value != last){
    if(value > last){
      debugSerial.println("Rotary More");
    } else {
      debugSerial.println("Rotary Less");
    }
    last = value;
  }
  ClickEncoder::Button b = encoder->getButton();
  if (b != ClickEncoder::Open) {
    switch (b) {
      case ClickEncoder::Pressed:
      break;
      case ClickEncoder::Held:
        debugSerial.println("Rotary Held");
  
      break;
      case ClickEncoder::Released:
        debugSerial.println("Rotary Release");
        break;
      case ClickEncoder::Clicked:
        debugSerial.println("Rotary Click");
        break;
      case ClickEncoder::DoubleClicked:
        debugSerial.println("Rotary Double Click");
        break;
    }
  }
}

