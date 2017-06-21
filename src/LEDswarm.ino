// TODO: convert Serial.printfs and println's to DEBUG statements we can turn on/off

//#define _TASK_MICRO_RES     // Turn on microsecond timing - painlessMesh does not support it (yet)

#define TIME_SYNC_INTERVAL  60000000  // Mesh time resync period, in us. 1 minute
#define FASTLED_ALLOW_INTERRUPTS 0
#define USE_GET_MILLISECOND_TIMER   // define our own millis() source for FastLED beat functions, in this case from mesh.getNodeTime

#include "LEDswarm.h"
#include "painlessMesh.h"
#include "ArduinoTapTempo.h"  // pio lib [--global] install https://github.com/dxinteractive/ArduinoTapTempo.git
#include "FastLED.h"

#ifdef    _TASK_MICRO_RES
#define   TASK_RES_MULTIPLIER   1000
#else
#define   TASK_RES_MULTIPLIER   1
#endif

#define   BUTTON_PIN        0

#define   MESH_PREFIX       "LEDforge.com LEDswarm"
#define   MESH_PASSWORD     "somethingSneaky"
#define   MESH_PORT         5555

#define   DEFAULT_PATTERN   0

#define   DEFAULT_BRIGHTNESS  80  // 0-255, higher number is brighter.
#define   NUM_LEDS          30
#define   DATA_PIN          2

CRGB leds[NUM_LEDS];
uint8_t maxBright = DEFAULT_BRIGHTNESS ;

uint8_t  currentPattern = DEFAULT_PATTERN ; // Which mode do we start with
uint8_t  nextPattern    = currentPattern ;
bool     firstPatternIteration = true ;    // if this pattern is being run for the first time

painlessMesh  mesh;
SimpleList<uint32_t> nodes;
String role = "MASTER" ; // default start out as master unless told otherwise

uint32_t currentBPM = 120 ; // default BPM of ArduinoTapTempo

ArduinoTapTempo tapTempo;
bool newBPMSet = true ;     // flag for when new BPM is set by button
//uint32_t tapTimer = 0 ;
#define HOLD_TIME 1000000   // wait time before auto tapping in new BPM, in uS
uint32_t holdTimer = 0 ;


#define TASK_CHECK_BUTTON_PRESS_INTERVAL    10   // in milliseconds
#define CURRENTPATTERN_SELECT_DEFAULT_INTERVAL     50   // default scheduling time for currentPatternSELECT, in milliseconds
Task taskCheckButtonPress( TASK_CHECK_BUTTON_PRESS_INTERVAL, TASK_FOREVER, &checkButtonPress);
Task taskCurrentPatternRun( CURRENTPATTERN_SELECT_DEFAULT_INTERVAL, TASK_FOREVER, &currentPatternRun);
Task taskSendMessage( TASK_SECOND * 5, TASK_FOREVER, &sendMessage ); // check every second if we have a new BPM / pattern to send
Task taskSelectNextPattern( TASK_SECOND * 15, TASK_FOREVER, &selectNextPattern);  // switch to next pattern every 15 seconds

void setup() {
  Serial.begin(115200);
  delay(1000); // Startup delay; let things settle down

  //mesh.setDebugMsgTypes( ERROR | MESH_STATUS | CONNECTION | SYNC | COMMUNICATION | GENERAL | MSG_TYPES | REMOTE ); // all types on
  mesh.setDebugMsgTypes( ERROR | STARTUP );  // set before init() so that you can see startup messages

  mesh.init( MESH_PREFIX, MESH_PASSWORD, MESH_PORT );
  mesh.onReceive(&receivedCallback);
  mesh.onNewConnection(&newConnectionCallback);
  mesh.onChangedConnections(&changedConnectionCallback);
  mesh.onNodeTimeAdjusted(&nodeTimeAdjustedCallback);
  mesh.onNodeDelayReceived(&delayReceivedCallback);

  nodes.push_back( mesh.getNodeId() ) ; // add our own ID to the list of nodes

  FastLED.addLeds<NEOPIXEL, DATA_PIN>(leds, NUM_LEDS);

  mesh.scheduler.addTask( taskSendMessage );
  mesh.scheduler.addTask( taskCheckButtonPress );
  mesh.scheduler.addTask( taskCurrentPatternRun );
  mesh.scheduler.addTask( taskSelectNextPattern );
  taskCheckButtonPress.enable() ;
  taskCurrentPatternRun.enable() ;
  taskSelectNextPattern.enable() ;


  Serial.print("Starting up... I am: ");
  Serial.println(mesh.getNodeId()) ;
} // end setup()


void loop() {
  mesh.update();
} // end loop()


// Better to have static and keep the memory allocated or not??
void sendMessage() {
  if( ! tapTempo.isChainActive() or (currentPattern != nextPattern) ) {
    static DynamicJsonBuffer jsonBuffer;
    static JsonObject& msg = jsonBuffer.createObject();

    currentPattern = nextPattern ;       // update our own running pattern
    currentBPM     = tapTempo.getBPM() ; // update our BPM with (possibly new) BPM
    newBPMSet      = false ;            // reset the flag

    msg["currentBPM"] = currentBPM;
    msg["currentPattern"] = currentPattern ;

    String str;
    msg.printTo(str);
    mesh.sendBroadcast(str);

    Serial.printf("%s %u: Sent broadcast message: ", role.c_str(), mesh.getNodeTime() );
    Serial.println(str);
  } else {
    Serial.printf("%s %u: No msg to send.\tBPM: %u\tPattern: %u\n", role.c_str(), mesh.getNodeTime(), currentBPM, currentPattern );
  }
} // end sendMessage()

#define SHORT_PRESS_MIN_TIME 50   // minimum time for a short press - debounce

void checkButtonPress() {
  static unsigned long buttonTimer = 0;
  static bool buttonActive = false;

  if( digitalRead(BUTTON_PIN) == LOW ) {
    if (buttonActive == false) {
      buttonActive = true;
      buttonTimer = millis();
    }
  } else {
    if (buttonActive == true) {
      buttonActive = false; // reset
      if ( millis() - buttonTimer > SHORT_PRESS_MIN_TIME ) {    // test if debounce is reached
        tapTempo.update(true); // update ArduinoTapTempo
        Serial.printf("%s %u: Button TAP. %u. BPM: ", role.c_str(), mesh.getNodeTime() );
        Serial.println(tapTempo.getBPM() );
        newBPMSet = true ;
      }
    }
  }
} // end checkButtonPress()

// This function is called by FastLED inside lib8tion.h. Requests it to use mesg.getNodeTime instead of internal millis() timer.
// Makes every synced!
uint32_t get_millisecond_timer() {
   return mesh.getNodeTime()/1000 ;
}
