#include <printf.h>
#include <nRF24L01.h>
#include <RF24_config.h>
#include <RF24.h>
#include<SPI.h>

const int CE = 7;
const int CSN = 6;
const int iRPin = 0;
int distanceMeasure;
const int ledPin = 5;
//Will have to change depending on what it's actually like on court
const int distanceThresh = 200;

volatile int commState;
volatile int rec;
volatile unsigned long startTime;
volatile unsigned long endTime;
volatile unsigned long netTime;

RF24 radio (CE, CSN);
const int numAddress = 2;
const uint8_t readAddresses[][6] = {"1", "2", "3", "4", "5"};
const uint8_t writeAddresses[][6] = {"1Node","2Node", "3Node", "4Node", "5Node"};

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
        commState = 1;
        startTime = millis();
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
        endTime = millis();
      }
      break;
    case 3:
        setupTransmit(writeAddresses[numAddress]);
        commState = 4;
      break;
    case 4:
      //radio is in transmit mode
      netTime = endTime - startTime;
      analogWrite(ledPin, 0);
      if (radio.write((unsigned long*)&netTime, sizeof(netTime))) {
        setupReceive();
        commState = 0;
      }
      break;
  }
}
