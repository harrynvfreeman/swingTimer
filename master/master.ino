#include <printf.h>
#include <nRF24L01.h>
#include <RF24_config.h>
#include <RF24.h>
#include<SPI.h>

typedef struct Node {
  uint8_t * message;
  Node * prev;
} Node;

typedef struct TimeNode {
  unsigned long val;
  TimeNode * prev;
} TimeNode;

typedef struct Queue {
  Node * head;
  Node * tail;
  int qSize;
} Queue;

typedef struct TimeQueue {
  TimeNode * head;
  TimeNode * tail;
  int qSize;
} TimeQueue;

Queue * createQueue() {
  Queue * queue = (Queue*)malloc(sizeof(Queue));
  queue->head = NULL;
  queue->tail = NULL;
  queue->qSize = 0;
  return queue;
}

TimeQueue * createTimeQueue() {
  TimeQueue * timeQueue = (TimeQueue*)malloc(sizeof(TimeQueue));
  timeQueue->head = NULL;
  timeQueue->tail = NULL;
  timeQueue->qSize = 0;
  return timeQueue;
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

void timeQueueInsert(TimeQueue * timeQueue, unsigned long val) {
  TimeNode * timeNode = (TimeNode*)malloc(sizeof(TimeNode));
  timeNode->val = val;
  timeNode->prev = NULL;
  if (timeQueue->qSize == 0) { //emtpy
    timeQueue->tail = timeNode;
  } else {
    timeQueue->head->prev = timeNode;
  }

  timeQueue->head = timeNode;
  timeQueue->qSize = timeQueue->qSize + 1;
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

unsigned long timeQueueRemove(TimeQueue * timeQueue) {
  if (timeQueue->qSize == 0) {
    return -1;
  }

  TimeNode * timeNode = timeQueue->tail;
  timeQueue->tail = timeQueue->tail->prev;
  if (timeQueue->qSize == 1) {
    timeQueue->head = NULL;
  }

  unsigned long val = timeNode->val;

  free(timeNode);
  timeQueue->qSize = timeQueue->qSize - 1;
  return val;
}

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

//Warning, when using arduino as usb to serial adapter, need to swap tx and rx for hm10

//common
void displayTime(TimeQueue * timeQueue);
int * transmitSignal;
int * radioLock;
Queue * messageQueue;
Queue * setOrderQueue;
Queue * radioQueue;
Queue * randomQueue;
TimeQueue * timeQueue;
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

//setOrderThread
void setOrderThread(int * state, Queue * queue, Queue * radioQueue, TimeQueue * timeQueue,
                    int * radioLock, int * transmitSignal, int * setOrderMessageIndex, 
                    uint8_t ** setOrderMessage, int numAddresses);
int * setOrderState;
uint8_t ** setOrderMessage;
int * setOrderMessageIndex;
//

//randomThread
void randomThread(int * state, Queue * queue, Queue * radioQueue, TimeQueue * timeQueue,
                    int * radioLock, int * transmitSignal, int * randomNumber,
                    int * randomIncrement, uint8_t ** randomMessage, int numAddresses);
int * randomState;
uint8_t ** randomMessage;
int * randomNumber;
int * randomIncrement;
//

//commThread
void commThread(int * state, Queue * queue, TimeQueue * timeQueue, int * transmitSignal, int * sendNum, unsigned long * rec);
int * commState;
int * sendNum;
unsigned long * rec;
const int debugPin = 9;
//

// message characters
const uint8_t eot = 0x74; //t
const uint8_t sot = 0x73; //s
const uint8_t setOrderMessageIndicator = 0x61; //a
const uint8_t randomMessageIndicator = 0x62; //b
//

const int CE = 7;
const int CSN = 6;
RF24 radio (CE, CSN);
const uint8_t writeAddresses[][6] = {"1", "2", "3", "4", "5"};
const uint8_t readAddresses[][6] = {"1Node","2Node", "3Node", "4Node", "5Node"};
                       
const int numAddresses = 3;

void setup() {
  //Radio
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
  //

  //common
  radioLock = (int*)malloc(sizeof(int));
  *radioLock = 0;
  transmitSignal = (int*)malloc(sizeof(int));
  *transmitSignal = 0;
  messageQueue = createQueue();
  setOrderQueue = createQueue();
  radioQueue = createQueue();
  randomQueue = createQueue();
  timeQueue = createTimeQueue();

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

  //setOrderThread
  setOrderState = (int*)malloc(sizeof(int));
  *setOrderState = 0;
  setOrderMessage = (uint8_t**)malloc(sizeof(uint8_t*));
  setOrderMessageIndex = (int*)malloc(sizeof(int));
  *setOrderMessageIndex = 0;

  //randomThread
  randomState = (int*)malloc(sizeof(int));
  *randomState = 0;
  randomMessage = (uint8_t**)malloc(sizeof(uint8_t*));
  randomNumber = (int*)malloc(sizeof(int));
  randomIncrement = (int*)malloc(sizeof(int));

  //commThread
  commState = (int*)malloc(sizeof(int));
  *commState = 0;
  sendNum = (int*)malloc(sizeof(int));
  *sendNum = 0;
  rec = (unsigned long *)malloc(sizeof(unsigned long));
  
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
  analogWrite(debugPin, 0);
}

void setupReceive() {
  radio.flush_tx();
  radio.startListening();
  analogWrite(debugPin, 255);
}

int8_t messageByteToInt(uint8_t messageByte) {
  if (messageByte == 0x30) {
    return 0;
  } else if (messageByte == 0x31) {
    return 1;
  } else if (messageByte == 0x32) {
    return 2;
  } else if (messageByte == 0x33) {
    return 3;
  } else if (messageByte == 0x34) {
    return 4;
  } else if (messageByte == 0x35) {
    return 5;
  } else if (messageByte == 0x36) {
    return 6;
  } else if (messageByte == 0x37) {
    return 7;
  } else if (messageByte == 0x38) {
    return 8;
  } else if (messageByte == 0x39) {
    return 9;
  }

  Serial.println("Error");
  return -1;
}

void loop() {
  blueToothThread(blueToothState, messageQueue, writeMessageIndex, message, maxMessageLength);
  messageHandlerThread(messageQueue, setOrderQueue, randomQueue);
  setOrderThread(setOrderState, setOrderQueue, radioQueue, timeQueue, radioLock,
                 transmitSignal, setOrderMessageIndex, setOrderMessage, numAddresses);
  randomThread(randomState, randomQueue, radioQueue, timeQueue,
                    radioLock, transmitSignal, randomNumber,
                    randomIncrement, randomMessage, numAddresses);
  commThread(commState, radioQueue, timeQueue, transmitSignal, sendNum, rec);
}

void commThread(int * state, Queue * queue, TimeQueue * timeQueue, int * transmitSignal, int * sendNum, unsigned long * rec) {
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
      if (radio.write(sendNum, sizeof(int))) {
        setupReceive();
        Serial.println("Written");
        Serial.println("Waiting to receive");
        *sendNum = *sendNum + 1;
        *state = 4;
      }
      break;
    case 4:
      //Radio is in receive mode
      analogWrite(debugPin, 255);
      if (radio.available()) {
        radio.read(rec, sizeof(unsigned long));
        timeQueueInsert(timeQueue, *rec);
        Serial.println("Received");
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

void setOrderThread(int * state, Queue * queue, Queue * radioQueue, TimeQueue * timeQueue,
                    int * radioLock, int * transmitSignal, int * setOrderMessageIndex, 
                    uint8_t ** setOrderMessage, int numAddresses) {
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
      if (*transmitSignal == 0) {
        uint8_t * message = *(setOrderMessage);
        if(*(message + *setOrderMessageIndex) == eot) {
          //Serial.println("Done sending message. Can release lock");
          *state = 0;
          *radioLock = 0;
          free(message);
          displayTime(timeQueue);
          return;
        }

        int8_t radioAddress = messageByteToInt(*(message + *setOrderMessageIndex));
         //Serial.print("Radio Address is: ");
         //Serial.println(radioAddress);
        if (radioAddress != -1 && radioAddress < numAddresses) {
          *transmitSignal = 1;
          uint8_t * radioMessage = (uint8_t*)malloc(sizeof(uint8_t));
          *(radioMessage) = (uint8_t)radioAddress;
          queueInsert(radioQueue, radioMessage);
        }
        *setOrderMessageIndex = *setOrderMessageIndex + 1;
      } 
  }
}

void randomThread(int * state, Queue * queue, Queue * radioQueue, TimeQueue * timequeue,
                    int * radioLock, int * transmitSignal, int * randomNumber,
                    int * randomIncrement, uint8_t ** randomMessage, int numAddresses) {
  int lockNum = 2;

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
      *(randomMessage) = queueRemove(queue);
      *randomNumber = 0;
      *randomIncrement = 0;
      int randomMessageIndex;
      randomMessageIndex = 1;
      boolean toProcess;
      toProcess = true;
      int8_t digit;
      uint8_t * message;
      message = *(randomMessage);
      while(toProcess) {
        digit = messageByteToInt(*(message + randomMessageIndex));
        Serial.print("Digit is: ");
        Serial.println(digit);
        if (digit == -1) {
          toProcess = false;
        } else {
          *randomNumber = 10*(*randomNumber) + digit;
          randomMessageIndex = randomMessageIndex + 1;
        }
      }
      free(message);
      
      *state = 2;
      break;
    case 2:
      if (*transmitSignal == 0) {
        if(*randomIncrement >= *randomNumber) {
          //Serial.println("Done sending message. Can release lock");
          *state = 0;
          *radioLock = 0;
          displayTime(timeQueue);
          return;
        }
        *transmitSignal = 1;
        int8_t radioAddress = random(0, numAddresses);
        uint8_t * radioMessage = (uint8_t*)malloc(sizeof(uint8_t));
        *(radioMessage) = (uint8_t)radioAddress;
        queueInsert(radioQueue, radioMessage);
        *randomIncrement = *randomIncrement + 1;
        Serial.print("Increment is: ");
        Serial.println(*randomIncrement);
        Serial.print("Num is: ");
        Serial.println(*randomNumber);
      }
    break;
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

void displayTime(TimeQueue * timeQueue) {
  int num = timeQueue->qSize;
  if (num == 0) {
    return;
  }

  unsigned long totalTime = 0;
  unsigned long * times = (unsigned long *)malloc(num*sizeof(unsigned long));

  int index = 0;
  while(timeQueue->qSize > 0) {
    unsigned long val = timeQueueRemove(timeQueue);
    totalTime = totalTime + val;
    *(times + index) = val;
    index = index + 1;
  }

  unsigned long averageTime = (unsigned long)((float)totalTime)/((float)num);

  Serial3.print("Total Time: ");
  Serial3.print(totalTime/1000);
  Serial3.print(" seconds and ");
  Serial3.print(totalTime % 1000);
  Serial3.println( "ms.");

  Serial3.print("Average Time: ");
  Serial3.print(averageTime/1000);
  Serial3.print(" seconds and ");
  Serial3.print(averageTime % 1000);
  Serial3.println( "ms.");

  for (int i = 0; i < num; i ++) {
    unsigned long val = *(times + i);
    Serial3.print("IndividualTime: ");
    Serial3.print(val/1000);
    Serial3.print(" seconds and ");
    Serial3.print(val % 1000);
    Serial3.println( "ms.");
  }

  free(times);
}
