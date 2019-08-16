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

osjob_t rxjob;
osjob_t timeoutjob;
static void tx_func (osjob_t* job);


//MOVING AVERAGE VARIABLES
#define MOV_AVG_SIZE 7
// moving window of incoming RSSIs
int rssis[MOV_AVG_SIZE] = { };
// current position in the moving average frame
int pos = 0; 

// returns the average of an array with num elements
float avg(int vals[], int num){
  int sum = 0;
  for(int i=0; i<num; i++)
    sum += vals[i];
  return sum/(double)num;
}

// Transmit an acknowledgement with sequence number and RSSI moving average
void acknowledge(byte seq, float rssi, osjobcb_t func) {
  os_radio(RADIO_RST); // Stop RX first
  delay(1); // Wait a bit, without this os_radio below asserts, apparently because the state hasn't changed yet
  // set first byte to be the sequence number
  LMIC.frame[0] = seq;
  LMIC.dataLen = 1;
  // convert RSSI to string (quick-and-dirty work around for sending byte value directly)
  String str = String(rssi);
  // copy payload into the frame char-by-char
  for(int i=0; i<str.length(); i++)
    LMIC.frame[LMIC.dataLen++] = str[i];
  // schedule next function
  LMIC.osjob.func = func;
  // transmit frame
  os_radio(RADIO_TX);
  Serial.println("    acknowledged.");
}

// Enable rx mode and call func when/if a packet is received
void rx(osjobcb_t func) {
  LMIC.osjob.func = func;
  LMIC.rxtime = os_getTime(); // RX _now_
  // Enable continuous RX (breaks upon packet reception)
  os_radio(RADIO_RXON);
  Serial.println("Receiving");
}

// called when receiver times out (after 3 transmission intervals)
static void rxtimeout_func(osjob_t *job) {
  Serial.println("--Timed out!");
  digitalWrite(LED_BUILTIN, LOW); // off
  // does not currently deactivate rx
}

// called when a packet is recieved.
// captures sequence number and sends back and acknowledgement
static void rx_func (osjob_t* job) {
  // Blink once to confirm reception and then keep the led on
  digitalWrite(LED_BUILTIN, LOW); // off
  delay(10);
  digitalWrite(LED_BUILTIN, HIGH); // on

  // Timeout RX after 3 periods without RX
  os_setTimedCallback(&timeoutjob, os_getTime() + ms2osticks(3*TX_INTERVAL), rxtimeout_func);

  // capture received sequence number
  byte seqno = 255;
  if(LMIC.dataLen > 0)
    seqno = (byte) LMIC.frame[0];

  //print important data to serial output
  Serial.print("    recieved ");
  Serial.print(LMIC.dataLen);
  Serial.print(" bytes: ");
  Serial.write(LMIC.frame+1, LMIC.dataLen-1);
  Serial.println();
  Serial.println("    seq=" + String(seqno));
  Serial.print("    rssi=");
  Serial.println(LMIC.rssi);  //dbM ?

  //add latest RSSI to moving average
  pos = pos % MOV_AVG_SIZE;
  rssis[pos++] = LMIC.rssi;

  //if sequence number is captured (not inital value) acknowledge else reschedule rx
  if(seqno < 255){
    acknowledge(seqno, avg(rssis, MOV_AVG_SIZE), postack_func);
  }else{
    os_setTimedCallback(&rxjob, os_getTime() + ms2osticks(0.9 * TX_INTERVAL), postack_func);
  }
}

//called after sending ACK (restarts listening)
static void postack_func (osjob_t* job) {
  rx(rx_func);
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

  // setup initial jobs
  os_setTimedCallback(&timeoutjob, os_getTime() + ms2osticks(3*TX_INTERVAL), rxtimeout_func);
  os_setCallback(&rxjob, postack_func);
}

void loop() {
  // execute scheduled jobs and events
  os_runloop_once();
}
