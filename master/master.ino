const int CE = 7;
const int CSN = 6;
const int debugPin = 9;

//IMPORTANT BUG
//MASTER Transmits start signal, but before setupReceive is done the other transmitter
//has written and we wait to read what has already been read.
//See uint32_t RF24::txDelay in documentation
//If set to 0, ensure 130uS delay after stopListening() and before any sends
//I also opened the pipe before calling receive which may have slowed things down
//Could be an arduino current limitting thing
//Was printing?

//WARNING
//If two radios receive on same address, master will receive acknowledge and move to next state.
//make sure to check sendNum (rec)

volatile int commState;
volatile int rec;
const int sendNum = 1;

#include<SPI.h>
#include "RF24.h"

RF24 radio (CE, CSN);
const uint8_t writeAddresses[][6] = {"1", "2", "3", "4", "5"};
const uint8_t readAddresses[][6] = {"1Node","2Node", "3Node", "4Node", "5Node"};
                       
const int numAddresses = 2;
volatile int sendAddress;
volatile int debugCounter = 0;

void setup() {
  Serial.begin(9600);
  radio.begin();
  radio.setPALevel(RF24_PA_MIN);
  radio.setDataRate(RF24_250KBPS);
  radio.txDelay = 250;
  radio.csDelay = 10;
  radio.setChannel(112);
  setupReadingPipes();
  pinMode(debugPin, OUTPUT);
  commState = 0;
  sendAddress = 0;
  delay(2000);
}

void setupReadingPipes() {
  for (int i = 1; i <= numAddresses; i++) {
    radio.openReadingPipe(i, readAddresses[i-1]);
  }
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
  switch (commState) {
    case 0:
      //set up transmit
      setupTransmit(writeAddresses[1]);
      commState = 1;
      break;
    case 1:
      //radio is in transmit mode
      analogWrite(debugPin, 0);
      Serial.println(debugCounter);
      if (radio.write((int*)&debugCounter, sizeof(debugCounter))) {
        setupReceive();
        Serial.print("Wrote: ");
        Serial.println(debugCounter);
        debugCounter = debugCounter + 1;
        commState = 2;
      }
      break;
    case 2:
      //Radio is in receive mode
      analogWrite(debugPin, 255);
      if (radio.available()) {
        rec = 0;
        radio.read((int*)&rec, sizeof(rec));
        Serial.print("Read: ");
        Serial.println(rec);
        commState = 3;
        sendAddress = 1 - sendAddress;
      }
      break;
    case 3:
      //There is a chance other transmitter did not
      //receive acknowledge.
      //wait here for a bit just so it can resend
      delay(5);
      commState = 0;
      break;
  }
}
