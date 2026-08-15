
//Dans EasyEDA, c'est le projet thermonuino-pilote-bt

#define STAT_LED PCINT1
#define ZONE_LED_1 PCINT2
#define ZONE_LED_2 PCINT3
#define ZONE_LED_3 PCINT4
#define ZONE_LED_4 PCINT5

#define ZONE_OPTO_1 PC2
#define ZONE_OPTO_2 PC3
#define ZONE_OPTO_3 PC4
#define ZONE_OPTO_4 INT0

#define RX_CONSOLE TXD
#define TX_CONSOLE RXD

#define LINKY PC0

#define BTN INT1 //En output, l'autre coté du bouton est en GND, pas de resistance pullup.

void setup() {
  
  pinMode(ZONE_LED_1, INPUT);
  pinMode(ZONE_LED_2, INPUT);
  pinMode(ZONE_LED_3, INPUT);
  pinMode(ZONE_LED_4, INPUT);

  digitalWrite(ZONE_LED_1, HIGH);
  digitalWrite(ZONE_LED_1, HIGH);
  digitalWrite(ZONE_LED_1, HIGH);
  digitalWrite(ZONE_LED_1, HIGH);

}

void loop() {
  // put your main code here, to run repeatedly:

  digitalWrite(ZONE_LED_1, HIGH);
  digitalWrite(ZONE_LED_2, HIGH);
  digitalWrite(ZONE_LED_3, HIGH);
  digitalWrite(ZONE_LED_4, HIGH);

  delay(1000);
  
  digitalWrite(ZONE_LED_1, LOW );
  digitalWrite(ZONE_LED_2, LOW );
  digitalWrite(ZONE_LED_3, LOW );
  digitalWrite(ZONE_LED_4, LOW );

  delay(1000);

}
