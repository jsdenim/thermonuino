


// send the message by bits
void sendMessage(byte repeats) {
  noInterrupts();


  for (repeats; repeats > 0; repeats--) {

    man.transmitArray(DATA_ARRAY_SIZE, data);
    delay(800);
 
  }

  ledBlink(4);
  interrupts();
  
 
}
