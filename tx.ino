/*******************************************************************************
 * Modification of code written by Matthijs Kooijman (2015)
 * Matthijs' code: https://github.com/dragino/Lora/tree/master/Lora%20Shield/Examples/lmic-raw-915
 *******************************************************************************/

#include <lmic.h>
#include <hal/hal.h>
#include <SPI.h>

#if !defined(DISABLE_INVERT_IQ_ON_RX)
#error This example requires DISABLE_INVERT_IQ_ON_RX to be set. Update \
       config.h in the lmic library to set it.
#endif

// How often to send a packet. 
#define TX_INTERVAL 6000

// Pin mapping
const lmic_pinmap lmic_pins = {
    .nss = 10,
    .rxtx = LMIC_UNUSED_PIN,
    .rst = 9,
    .dio = {2, 6, 7},
};

// These callbacks are only used in over-the-air activation, so they are
// left empty here (we cannot leave them out completely unless
// DISABLE_JOIN is set in config.h, otherwise the linker will complain).
void os_getArtEui (u1_t* buf) { }
void os_getDevEui (u1_t* buf) { }
void os_getDevKey (u1_t* buf) { }

void onEvent (ev_t ev) {
}

osjob_t txjob;
osjob_t timeoutjob;
static void tx_func (osjob_t* job);


//VARIABLES FOR SEQUENCING, DATA PAYLOAD AND PACKET TRACKING
// current and last sequence number transmitted
byte seqno = 0, lastSeq = 255;
// data to be transmitted
char* payload = "cognito ergo sum";
// the number of packets sent and the number correctly acknowledged
int sent = 0, correct = 0;


// Transmit the given string and call the given function afterwards
void tx(const char *str, osjobcb_t func) {
  os_radio(RADIO_RST); // Stop RX first
  delay(1); // Wait a bit, without this os_radio below asserts, apparently because the state hasn't changed yet
  // set first byte to be the sequence number
  LMIC.frame[0] = seqno;
  LMIC.dataLen = 1;
  // copy payload into the frame char-by-char
  while (*str)
    LMIC.frame[LMIC.dataLen++] = *str++;
  // schedule next function
  LMIC.osjob.func = func;
  // transmit frame
  os_radio(RADIO_TX);
  // print status update of transmissions
  if(sent>0 && sent%10 == 0){
    Serial.println();
    Serial.println("[Status]" + String(sent) + " messages sent, " + String(correct) + " successful (" + String(correct*100/(double)sent) + "%)");
    Serial.println();
  }
  // print different message based on sequence number
  if(seqno == lastSeq){
    Serial.println("    retransmitting (seq = " + String(seqno) + ")");
  }else{
    Serial.println("Transmitting (seq = " + String(seqno) + ")");
  }
  // update last sequence number
  lastSeq = seqno;
}

// Enable rx mode and call func when/if a packet is received
void rx(osjobcb_t func) {
  LMIC.osjob.func = func;
  LMIC.rxtime = os_getTime(); // RX _now_
  // Enable continuous RX (breaks upon packet reception)
  os_radio(RADIO_RXON);
}

// called upon successful receiving a [ACK] packet
// checks sequence number and updates sequence num/data if required
static void rx_func (osjob_t* job) {
  // Blink once to confirm reception and then keep the led on
  digitalWrite(LED_BUILTIN, LOW); // off
  delay(10);
  digitalWrite(LED_BUILTIN, HIGH); // on

  // capture returned sequence number
  byte ret_seqno = 255;
  if(LMIC.dataLen > 0){
    ret_seqno = LMIC.frame[0];
    Serial.print("    ACK received (seq = " + String(ret_seqno) + "). ");
  }
  // check sequence number
  if(ret_seqno < 255 && ret_seqno == seqno){
    //exchange successful
    Serial.print("Success! (RSSI = ");
    Serial.write(LMIC.frame+1, LMIC.dataLen-1);
    Serial.println(")");
    // record correct ACK
    correct++;
    // toggle sequence number (1->0, 0->1)
    seqno = 1 - seqno;
    // could update payload here if necessary
  }else{
    Serial.println("Bad ACK");
  }
}

// called after transmission is successfully completed
static void txdone_func (osjob_t* job) {
  sent++; //records number of sent messages
  rx(rx_func);
}

// Callback function for transmitting
static void tx_func (osjob_t* job) {
  // send data
  tx(payload, txdone_func);
  // shedule next transmission
  os_setTimedCallback(job, os_getTime() + ms2osticks(TX_INTERVAL + random(500)), tx_func);
}

// application entry point
void setup() {
  Serial.begin(9600); //115200);
  Serial.println("Starting");
  #ifdef VCC_ENABLE
  // For Pinoccio Scout boards
  pinMode(VCC_ENABLE, OUTPUT);
  digitalWrite(VCC_ENABLE, HIGH);

  Serial.println(LMIC.freq); //from http://wiki.dragino.com/index.php?title=Lora_Shield
  
  delay(1000);
  #endif

  pinMode(LED_BUILTIN, OUTPUT);

  // initialize runtime env
  os_init();
  
  // Transmission frequency
  LMIC.freq = 915000000;
    
  // Maximum TX power use 27;
  LMIC.txpow = 2; //2 to 15
  // Sets data rate and spread factor
  LMIC.datarate = DR_SF7;
  LMIC.rps = updr2rps(LMIC.datarate);

  Serial.println("Started");
  Serial.flush();

  // setup initial job
  os_setCallback(&txjob, tx_func);
}

void loop() {
  // execute scheduled jobs and events
  os_runloop_once();
}
