#include <Manchester.h>

#define DATA_ARRAY_SIZE 3
uint8_t data[DATA_ARRAY_SIZE] = {9, 9, 9};

#define TEMP_IDX_START 0
#define TEMP_IDX_END 3




// SETTINGS
#define INTVAL 5  // frequency of sending the data in seconds.
#define CHAN 1  // set channel bit (0=CH1,1=CH2,2=CH3, )
#define REP 3  // signal repeats (default=7x)
//#define ID 1318  // device id (1280-1535) [when disabled, random id on start]

// sleep function libraries
#include <avr/sleep.h>
#include <avr/interrupt.h>
#include <avr/power.h>
#include <avr/wdt.h>


// initialize DHT sensor library
#include <dht.h>
dht DHT;

#define TX_PIN  3
#define PIR_PIN  PB1
#define DHT_PIN  PB2
#define LED_PIN PB0

// variables of sleep code
volatile bool watchdogActive = false;
byte sleepCycles = 0;

// buffer to store message ready-to-send by bits
byte message[36];

long scale_constant = 112640L; // default scale_constant (1.1*1024*100)

// define watchdog timer interrupt action
ISR(WDT_vect) {
  watchdogActive = true;  // set flag
}

volatile bool excited = false;

void excitationFnc (){
  excited = true;
   //sleep();  // go to sleep!
}

void setup() {
  //OSCCAL = 0x4E;  // internal oscillator calibration value (important! see manual...)

  man.workAround1MhzTinyCore(); //add this in order for transmitter to work with 1Mhz Attiny85/84
  man.setupTransmit(TX_PIN, MAN_1200);
  
  pinMode(LED_PIN, OUTPUT);  // STATUS LED
  pinMode(TX_PIN, OUTPUT);  // RF TRANSMITTER
  pinMode(DHT_PIN, INPUT);  // DHT SENSOR

  pinMode(PIR_PIN, INPUT);  // PIR

  attachInterrupt(1, excitationFnc, RISING);
  

  ledBlink(8);

  calibration(); // calibrate battery readings if VCC1 present


  initMessage();  // set constant message bits (id,ch)

  updateMessage(1);  // refresh message data (bat,txmode,temp,hum)

  sendMessage(REP);  // send the message on rf transmitter for 'REP' times


  setup_watchdog();  // set 8s watchdog timer interrupt

  resetData();
  
}

void loop() {


  if(excited){
   // ledBlink(5);
    excited = false;
  } else {
    //ledBlink(1);
  }
  
  if (watchdogActive) {  // if there was a watchdog wakeup
    watchdogActive = false;  // reset the flag
    sleepCycles += 1;

    if (sleepCycles >= 8) {
      //resetData();

      updateMessage(0);


      sendMessage(REP);

      sleepCycles = 0;  // reset counter
    }
  }

  sleep();  // go to sleep!
}


// A very simple led blinking function for feedback
void ledBlink(byte n) {
  for (n; n>0; n--) {
    digitalWrite(LED_PIN, HIGH);
    delay(150);
    digitalWrite(LED_PIN, LOW);
    delay(150);
  }
}

void resetData(){
  for(int i = 0; i < DATA_ARRAY_SIZE; i++){
    data[i] = 0;
  }
}
