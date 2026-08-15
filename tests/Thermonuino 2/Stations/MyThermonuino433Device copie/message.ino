// Function used to set constant message bits at start
void initMessage() {
  #if not defined (ID)
    randomSeed(analogRead(A3)); // generate random ID on every restart...
    const int ID = random(1280,1535); // ...if there is no defined ID
  #endif
  
  // write to message array
  //convertBit(ID, 0);
  //convertBit(CHAN-1, 14);
}

// helper function to update message data
void updateMessage(byte mode) {
  checkBattery();
  message[13] = mode;  // 0:auto, 1:forced
  updateDHT();
}


// read and convert TEMP and HUM to bits and fill to message array
void updateDHT() {
    pinMode(DHT_PIN, INPUT);  // DHT SENSOR
    delay(500);
    int chk = DHT.read22(DHT_PIN);
    
    switch (chk)
    {
    case DHTLIB_OK:
    
        int IntegerPart = (int)(DHT.temperature);
        int DecimalPart = 10000 * (DHT.temperature - IntegerPart); //10000 b/c my float values always have exactly 4 decimal places
       // Serial.println (DecimalPart);
    
        data[0] = DHT.temperature;
        data[1] = 0;
        data[2] = DHT.humidity;
        break;
    case DHTLIB_ERROR_CHECKSUM:
        data[0] = 99;
        data[1] = 1;
        break;
    case DHTLIB_ERROR_TIMEOUT:
        data[0] = 99;
        data[1] = 2;
        break;
    case DHTLIB_ERROR_CONNECT:
        data[0] = 99;
        data[1] = 3;
        break;
    case DHTLIB_ERROR_ACK_L:
        data[0] = 99;
        data[1] = 4;
        break;
    case DHTLIB_ERROR_ACK_H:
        data[0] = 99;
        data[1] = 5;
        break;
    default:
        data[0] = 99;
        data[1] = 6;
        break;
    }
    


}

// function to measure voltage
int readVcc() {
  ADMUX = _BV(MUX3) | _BV(MUX2);  // set reference to vcc and measurement to int. 1.1v reference.
  delay(2); // wait for vref to settle
  ADCSRA |= _BV(ADSC); // start conversion
  while (bit_is_set(ADCSRA,ADSC)); // wait here while measuring

  uint8_t low  = ADCL; // must read ADCL first - it then locks ADCH  
  uint8_t high = ADCH; // unlocks both
 
  long vcc_reading = (high<<8) | low;
  int result = round(scale_constant / vcc_reading); // calculate VCC (in mV)
  return result;
}

void checkBattery() {
  int voltage = readVcc();
  
  if (voltage < 370) message[12] = 0;  // low
  else message[12] = 1;  // charged
}

// Improve accuracy of VCC readings with calibration
void calibration() {  //... if VCC1 is available
  #if defined (VCC1)
    unsigned int readings = 0;
    
    for (byte i=0;i<10;i++) {  // get 10 initial readings
      readings += readVcc();
      delay(25);  // gives more stable results
    }

    int vcc2 = readings / 10;  // calculate the average

    // ###### ###### could check here those readings later...
    int refvolt = 110L*VCC1/vcc2;
    scale_constant = refvolt * 1024L;
  #endif
}
