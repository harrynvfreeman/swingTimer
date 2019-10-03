const int CE = 7;
const int CSN = 6;
const int iRPin = 0;
int distanceMeasure;
const int ledPin = 5;
//Will have to change depending on what it's actually like on court
const int distanceThresh = 200;

volatile int commState;
volatile int rec;
const int sendNum = 2;
volatile int debugCounter = 0;

#include<SPI.h>
#include "RF24.h"

RF24 radio (CE, CSN);
const int numAddress = 1;
const uint8_t readAddresses[][6] = {"1", "2", "3", "4", "5"};
const uint8_t writeAddresses[][6] = {"1Node","2Node", "3Node", "4Node", "5Node"};

//const uint8_t readAddress[6] = {readAddresses[numAddress]};
//const uint8_t writeAddress[6] = {writeAddresses[numAddress]};

void setup() {
  Serial.begin(9600);
  radio.begin();
  radio.setPALevel(RF24_PA_MIN);
  radio.setDataRate(RF24_250KBPS);
  radio.txDelay = 250;
  radio.csDelay = 10;
  radio.setChannel(112);
  radio.openReadingPipe(1, readAddresses[numAddress]);
  pinMode(ledPin, OUTPUT);
  commState = 0;
  setupReceive();
}

void setupTransmit(const uint8_t *address) {
  radio.stopListening();
  radio.flush_rx();
  radio.flush_tx();
  radio.openWritingPipe(address);
}

void setupReceive() {
  radio.flush_tx();
  radio.startListening();
}

void loop() {
  switch(commState) {
    case 0:
      //Radio is in receive mode
      analogWrite(ledPin, 255);
      if (radio.available()) {
        rec = 0;
        radio.read((int*)&rec, sizeof(rec));
        Serial.print("Read: ");
        Serial.println(rec);
        commState = 1;
      }
      break;
    case 1:
      //There is a chance other transmitter did not
      //receive acknowledge.
      //wait here for a bit just so it can resend
      delay(5);
      commState = 2;
      break;
    case 2:
      //Radio is still in receive mode
      //There is chance transmitter did not get acknowledge
      //Stay in receive mode 
      analogWrite(ledPin, 20);
      distanceMeasure = analogRead(iRPin);
      if (distanceMeasure > distanceThresh) {
        commState = 3;
      }
      break;
    case 3:
      distanceMeasure = analogRead(iRPin);
      //if (distanceMeasure < distanceThresh) {
      if (true) {
        setupTransmit(writeAddresses[numAddress]);
        commState = 4;
      }
      break;
    case 4:
      //radio is in transmit mode
      analogWrite(ledPin, 0);
      Serial.println(debugCounter);
      if (radio.write((int*)&debugCounter, sizeof(debugCounter))) {
        setupReceive();
        Serial.print("Wrote: ");
        Serial.println(debugCounter);
        debugCounter = debugCounter + 1;
        commState = 0;
      }
      break;
  }
}
