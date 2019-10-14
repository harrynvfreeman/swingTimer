#include <printf.h>
#include <nRF24L01.h>
#include <RF24_config.h>
#include <RF24.h>
#include<SPI.h>

typedef struct Node {
  uint8_t * message;
  Node * prev;
} Node;

typedef struct Queue {
  Node * head;
  Node * tail;
  int qSize;
} Queue;

Queue * createQueue() {
  Queue * queue = (Queue*)malloc(sizeof(Queue));
  queue->head = NULL;
  queue->tail = NULL;
  queue->qSize = 0;
  return queue;
}

//assuming same message pointer isn't inserted twice
void queueInsert(Queue * queue, uint8_t * message) {
  Node * node = (Node*)malloc(sizeof(Node));
  node->message = message;
  node->prev = NULL;
  if (queue->qSize == 0) { //emtpy
    queue->tail = node;
  } else {
    queue->head->prev = node;
  }

  queue->head = node;
  queue->qSize = queue->qSize + 1;
}

uint8_t * queueRemove(Queue * queue) {
  if (queue->qSize == 0) {
    return NULL;
  }

  Node * node = queue->tail;
  queue->tail = queue->tail->prev;
  if (queue->qSize == 1) {
    queue->head = NULL;
  }

  uint8_t * message = node->message;

  free(node);
  queue->qSize = queue->qSize - 1;
  return message;
}

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

//What I learned:
//nrf24 library can be wrong.  need to flush rx after stopListen
//need to wait greater than 1.2 ms (I put 5) in case ack was dropped
//for another cycle

//Had to add a 100nF cap in parallel with 100uF cap 
//on power supply for radios

//Remember to comment radio supplied higher power go into write mode

//Set name AT+NAME[name] max lenght 12
//Set code AT+PASS[code] 000000-999999 I chose 201200
//Set auth type AT+TYPE2

//for timing, todo
volatile unsigned long rec;
volatile unsigned long seconds;
volatile unsigned long mils;
//

//send info, irrelevant right now
const int sendNum = 1;
//


//common
int * transmitSignal;
int * radioLock;
Queue * messageQueue;
Queue * setOrderQueue;
Queue * radioQueue;
Queue * randomQueue;
//

//blueToothThread
void blueToothThread(int * state, Queue * queue, int * writeMessageIndex, uint8_t ** message, int * maxMessageLength);
int * blueToothState;
int * writeMessageIndex;
int * maxMessageLength;
uint8_t ** message;
//

//messageHandlerThread
void messageHandlerThread(Queue * messageQueue, Queue * setOrderQueue, Queue * randomQueue);
int * messageHandlerState;
//

//setOrderThreadVariables
void setOrderThread(int * state, Queue * queue, Queue * radioQueue,
                    int * radioLock, int * transmitSignal, int * setOrderMessageIndex, 
                    uint8_t ** setOrderMessage);
int * setOrderState;
uint8_t ** setOrderMessage;
int * setOrderMessageIndex;
//

//commThread
void commThread(int * state, Queue * queue, int * transmitSignal);
int * commState;
//

// message characters
const uint8_t eot = 0x38; //8
const uint8_t sot = 0x39; //9
const uint8_t setOrderMessageIndicator = 0x31; //1
const uint8_t randomMessageIndicator = 0x32; //2
//

RF24 radio (CE, CSN);
const uint8_t writeAddresses[][6] = {"1", "2", "3", "4", "5"};
const uint8_t readAddresses[][6] = {"1Node","2Node", "3Node", "4Node", "5Node"};
                       
const int numAddresses = 3;

void setup() {
  ////Radio
  Serial.begin(9600);
  Serial3.begin(9600);
  radio.begin();
  radio.setPALevel(RF24_PA_MIN);
  radio.setDataRate(RF24_250KBPS);
  radio.txDelay = 250;
  radio.csDelay = 10;
  radio.setChannel(112);
  setupReadingPipes();
  pinMode(debugPin, OUTPUT);
  ////

  ////common
  radioLock = (int*)malloc(sizeof(int));
  *radioLock = 0;
  transmitSignal = (int*)malloc(sizeof(int));
  *transmitSignal = 0;
  messageQueue = createQueue();
  setOrderQueue = createQueue();
  radioQueue = createQueue();
  randomQueue = createQueue();

  //bluetoothThread
  blueToothState = (int*)malloc(sizeof(int));
  *blueToothState = 0;
  writeMessageIndex = (int*)malloc(sizeof(int));
  *writeMessageIndex = 0;
  maxMessageLength = (int*)malloc(sizeof(int));
  *maxMessageLength = 10;
  message = (uint8_t**)malloc(sizeof(uint8_t*));

  //messageHandlerThread
  messageHandlerState = (int*)malloc(sizeof(int));
  *messageHandlerState = 0;

  //setOrderThreadVariables
  setOrderState = (int*)malloc(sizeof(int));
  *setOrderState = 0;
  setOrderMessage = (uint8_t**)malloc(sizeof(uint8_t*));
  setOrderMessageIndex = (int*)malloc(sizeof(int));
  *setOrderMessageIndex = 0;

  //commThread
  commState = (int*)malloc(sizeof(int));
  *commState = 0;

  
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
//  Serial.print("Transmit Address is: ");
//  Serial.println(*address);
  radio.openWritingPipe(address);
}

void setupReceive() {
  radio.flush_tx();
  radio.startListening();
}

int8_t messageByteToInt(uint8_t messageByte) {
  if (messageByte == 0x61) {
    return 0;
  } else if (messageByte == 0x62) {
    return 1;
  } else if (messageByte == 0x63) {
    return 2;
  }

  Serial.println("Error");
  return -1;
}

void loop() {
  blueToothThread(blueToothState, messageQueue, writeMessageIndex, message, maxMessageLength);
  messageHandlerThread(messageQueue, setOrderQueue, randomQueue);
  setOrderThread(setOrderState, setOrderQueue, radioQueue, radioLock, 
                 transmitSignal, setOrderMessageIndex, setOrderMessage);
  commThread(commState, radioQueue, transmitSignal);
}

void commThread(int * state, Queue * queue, int * transmitSignal) {
  switch (*state) {
    case 0:
      if (queue->qSize > 0) {
        uint8_t * address = queueRemove(queue);
        int intAddress = (int)(*address);
        //set up transmit
        setupTransmit(writeAddresses[intAddress]);
        Serial.print("Write Address is: ");
        Serial.println(intAddress);
        free(address);
        *state = 3;
      }
      break;
    case 3:
      //radio is in transmit mode
      analogWrite(debugPin, 0);
      if (radio.write((int*)&sendNum, sizeof(sendNum))) {
        Serial.println("Written");
        setupReceive();
        Serial.println("Waiting to receive");
        *state = 4;
      }
      break;
    case 4:
      //Radio is in receive mode
      analogWrite(debugPin, 255);
      if (radio.available()) {
        rec = 0;
        radio.read((unsigned long*)&rec, sizeof(rec));
        seconds = rec / 1000;
        mils = rec % 1000;
        //Serial.print("Time took: ");
        //Serial.print(seconds);
        //Serial.print(" and ");
        //Serial.print(mils);
        //Serial.println(" ms.");
        //Serial.println(rec);
        *state = 5;
      }
      break;
    case 5:
      //There is a chance other transmitter did not
      //receive acknowledge.
      //wait here for a bit just so it can resend
      delay(5);
      *state = 0;
      *transmitSignal = 0;
      break;
  }
}

void setOrderThread(int * state, Queue * queue, Queue * radioQueue,
                    int * radioLock, int * transmitSignal, int * setOrderMessageIndex, 
                    uint8_t ** setOrderMessage) {
  int lockNum = 1;

  switch (*state) {
    case 0:
      if (queue->qSize > 0) {
        //Serial.println("Message detected in setOrder Queue");
        *state = 1;
      }
      break;
    case 1:
      if (*(radioLock) != 0) {
          //Serial.println("Lock not available, must wait");
          return;
      }
     // Serial.println("Acquiring Lock");
      *(radioLock) = lockNum;
      *(setOrderMessage) = queueRemove(queue);
      *(setOrderMessageIndex) = 1;
      *state = 2;
      break;
    case 2:
      //if (*transmitSignal == 0) {
        uint8_t * message = *(setOrderMessage);
        if(*(message + *setOrderMessageIndex) == eot) {
          //Serial.println("Done sending message. Can release lock");
          *state = 0;
          *radioLock = 0;
          free(message);
          return;
        }

        int8_t radioAddress = messageByteToInt(*(message + *setOrderMessageIndex));
         //Serial.print("Radio Address is: ");
         //Serial.println(radioAddress);
        if (radioAddress != -1) {
          *transmitSignal = 1;
          uint8_t * radioMessage = (uint8_t*)malloc(sizeof(uint8_t));
          *(radioMessage) = (uint8_t)radioAddress;
          queueInsert(radioQueue, radioMessage);
        } else {
           //Serial.println("Unrecognized radioAddress");
        }
        *setOrderMessageIndex = *setOrderMessageIndex + 1;
      //} else {
        //Serial.println("Radio still transmitting");
      //}
  }

  
}

void messageHandlerThread(Queue * messageQueue, Queue * setOrderQueue, 
                          Queue * randomQueue) {
  if (messageQueue->qSize > 0) {
    //Serial.println("Message in message queue detected");
    uint8_t * message = queueRemove(messageQueue);

    switch(*(message)) {
      case setOrderMessageIndicator:
        //Serial.println("Sending to setOrder Queue");
        queueInsert(setOrderQueue, message);
        break;
      case randomMessageIndicator:
        //Serial.println("Sending to random Queue");
        queueInsert(randomQueue, message);
        break;
      default:
        //Serial.println("Freeing message");
        free(message);
        return;
    }
  }

}

void blueToothThread(int * state, Queue * queue, int * writeMessageIndex, uint8_t ** message, int * maxMessageLength) {
  uint8_t incomingByte;
  switch(*state) {
    //If in 0, have not started message
    //Check for sot
    case 0:
      if (Serial3.available()) {
        incomingByte = Serial3.read();
        Serial.println(incomingByte, HEX);
        if (incomingByte == sot) {
          //Serial.println("SOT detected");
          *state = 1;
          *message = (uint8_t*)malloc((*maxMessageLength)*sizeof(uint8_t));
          *writeMessageIndex = 0;
        }
      }
      break;
    case 1:
      //Wait for end of tranmission and add to message
      if (Serial3.available()) {
        incomingByte = Serial3.read();
        Serial.println(incomingByte, HEX);

        if (incomingByte == sot) { //restart message
          //Serial.println("Restarting Message");
          *writeMessageIndex = 0;
          return;
        }

        if (incomingByte == eot) {
          if (*writeMessageIndex == 0) { //no message
            //Serial.println("Message had length 0");
            *state = 0;
            free(*message);
            return;
          } else {
            //Serial.println("Message Completed");
            *(*message + *writeMessageIndex) = incomingByte;
            *state = 2; //eot
          }
            return;
        }
        //otherwise add message.  If message is too long, stop.
        //-1 because of eom
        if (*writeMessageIndex >= *maxMessageLength - 1) {
          //Serial.println("Message Too Long");
          *state = 0;
          free(*message);
          return;
        }
        
        *(*message + *writeMessageIndex) = incomingByte;
        *writeMessageIndex = *writeMessageIndex + 1;
      }
      break;
    case 2:
      //Serial.println("Inserting into Message Queue");
      queueInsert(queue, *message);
      *state = 0;
      break;
  }
}
