#include "Arduino.h"

unsigned char DEMO = "D";
unsigned char FONCTION_OK = "O";

byte ledPin;
byte btnPin;
volatile boolean btnToConsumes;
boolean demoMode = false;
volatile unsigned long lastButtonPush = 0;
    
void interruptHandler() {
      if((millis() - lastButtonPush) < (unsigned long)250){
        return;
      }

      btnToConsumes = true;
      lastButtonPush = millis();
}


void StatutSetup(byte ledPinN, byte btnPinN){

      ledPin = ledPinN;
      btnPin = btnPinN;

      pinMode(btnPin, INPUT_PULLUP);
      pinMode(ledPin, OUTPUT);
      digitalWrite(ledPin, HIGH);
      
      btnToConsumes = false;
      attachInterrupt(digitalPinToInterrupt(btnPin), interruptHandler, RISING);

      
}

bool StatutLoopAndBreake(){


      if(btnToConsumes){
        btnToConsumes = false;
        demoMode = !demoMode;
      } 

      if( !demoMode){
         digitalWrite(ledPin, LOW);
         return false;
      } else {

        digitalWrite(ledPin, HIGH);

        for(byte e = 0; e < ZONES; e++){
          zones[e].forcerEtat(false);
        }

        btnToConsumes = false;
        
        for(byte e = 0; e < ZONES && !btnToConsumes; e++){
          zones[e].forcerEtat(true);
          for(byte sec = 0; sec < 5 && !btnToConsumes; sec++){
            delay(900);
            digitalWrite(ledPin, LOW);
            delay(100);
            digitalWrite(ledPin, HIGH);
          }
          
          zones[e].forcerEtat(false);
        }

        digitalWrite(ledPin, LOW);

        return true;
      } 
      

      
      
      
}




