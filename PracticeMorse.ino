/***************************************************************
 * 06/14/2023  Edward Williams  Practice Morse
 * 
 * Practice Morse uses a LilyGo TTGO T Display to make a Morse code tester. 
 * A piezo speaker is connected plus to pin 26 and negative to Ground.
 * A 3.5mm female connector is connected tip (dot) to pin 13, R1 (dash) to 
 * pin 12, R2 to Ground. A morse paddle can be plugged into the 3.5mm connector. 
 * 
 * A big thank you goes to W.Kraml (OE1WKL) for sharing his code for the 
 * Morserino-32. I used his code as a base to start from.
 * 
 * Practice Morse has CW Keyer, CW Generator, Echo Trainer and Koch Trainer
 * functions.
 * 
 * I created a 3D printed box to hold the components along with a Lipo battery.
 * 
 ***************************************************************/

const String VERSION = "V2.0.0";

#include "Preferences.h"   // ESP 32 library for storing things in NVS
#include "Pangodream_18650_CL.h"
#include "SPIFFS.h" 
#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>

Preferences pref;  // use the Preferences library for storing/retrieving objects

//  battery stuff
#define BATTERY_PIN 14
#define BATTERY_ICON_WIDTH 35
#define BATTERY_ICON_HEIGHT 18
#define BATTERY_ICON_POS_X (tft.width() - BATTERY_ICON_WIDTH)
#define BATTERY_TEXT_WIDTH (tft.textWidth("Chrg", 2) + 2)
#define BATTERY_TEXT_HEIGHT BATTERY_ICON_HEIGHT
#define BATTERY_TEXT_POS_X ((BATTERY_ICON_POS_X - BATTERY_TEXT_WIDTH) - 4)
#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

#define MIN_USB_VOL_NO_CHARGE 4.85
#define MIN_USB_VOL_CHARGE 4.4
#define MIN_BATTERY_VOLTAGE 3.2
#define ADC_PIN 34
#define CONV_FACTOR 1.8
#define READS 20

Pangodream_18650_CL BL(ADC_PIN, CONV_FACTOR, READS);
char *batteryImages[] = {"/battery_01.jpg", "/battery_02.jpg", "/battery_03.jpg", "/battery_04.jpg", "/battery_05.jpg"};

int batteryCheckState = 6;  // keep track of what battery status to show
  // 0 - showed < 20% icon
  // 1 - showed < 50% icon
  // 2 - showed < 80% icon
  // 3 - showed >= 80% icon
  // 6 - nothing showed yet
  // 10 - showed < 20% icon while charging
  // 11 - showed < 50% icon while charging
  // 12 - showed < 80% icon while charging
  // 13 - showed >= 80% icon while charging
  // 14 - showed charging icon
  // 20 - battery icon hidden (for blanking very low)
double batteryVLevel;
int imgNumPrev = 6;
int batteryLevelPrev = 101;
unsigned long timeout_battery = 0LL;  

// end battery stuff

// screen stuff
TFT_eSPI tft = TFT_eSPI();
uint16_t CurX, CurY;  // current X and Y position of cursor on the display
uint16_t *ScreenBlock;  // a buffer to store a copy of the screen for moving around
  // Use following line number start positions and sizes:
  // 1 - 0,0 170,26; 2 - 0,31 240,26; 3 - 0,57 240,26; 4 - 0,83240,26; 5 - 0,109 240,26
// end screen stuff

//  button stuff
#include <Button2.h>

#define BTN_UP 35  // upper button on board
#define BTN_DWN 0  // lower button on board
Button2 btnUp(BTN_UP); // Initialize the up button
Button2 btnDwn(BTN_DWN); // Initialize the down button

// button usages
enum buttonUsageStates {
  IDLE, SCROLL 
};
buttonUsageStates buttonUsage = IDLE;

enum buttonPressStates { NONE, UPSHORT, UPLONG, DOWNSHORT, DOWNLONG };
buttonPressStates buttonPressed = NONE;
//  end button stuff


//  tone stuff
// Change this depending on where you put your piezo buzzer
#define TONE_OUTPUT_PIN 26

// The ESP32 has 16 channels which can generate 16 independent waveforms
// I'm using PWM channel 0 here
#define TONE_PWM_CHANNEL 0 

// use 12 bit precission for LEDC timer
#define LEDC_TIMER_12_BIT 12
//  end tone stuff

//  morse stuff

#include "PracticeMorse.h"
#include "abbrev.h"           // common CW abbreviations
#include "english_words.h"    // common English words

/*
* Typical timings:
* short mark, dot or 'dit' (·) — 'dot duration' is one unit long
* longer mark, dash or 'dah' (–) — three units long
* inter-element gap between the dots and dashes within a character
*   - one unit long
* short gap (between letters) — three units long
* medium gap (between words) — seven units long
* 
* Based upon a 50 dot duration standard word such as PARIS, the time
* for one dot duration or one unit can be computed by the formula:
* T = 1200 / W
* or
* T = 6000 / C
* Where: T is the unit time, or dot duration, in milliseconds,
* W is the speed in wpm, and C is the speed in cpm.
*/


// paddle pins
#define RIGHT_PIN 12
#define LEFT_PIN 13
int leftPin, rightPin;

//  keyerControl bit definitions
#define     DIT_L      0x01  // Dit latch
#define     DAH_L      0x02  // Dah latch
#define     DIT_LAST   0x04  // Dit was last processed element

//  Global Keyer Variables
unsigned char keyerControl = 0; // this holds the latches for the paddles and the DIT_LAST latch, 

enum KEYERSTATES { IDLE_STATE, DIT, DAH, KEY_START, KEYED, INTER_ELEMENT };
KEYERSTATES keyerState;

enum DISPLAY_TYPE { NO_DISPLAY, DISPLAY_BY_CHAR, DISPLAY_BY_WORD };

enum random_OPTIONS { OPT_ALL, OPT_ALPHA, OPT_NUM, OPT_PUNCT, OPT_PRO, 
  OPT_ALNUM, OPT_NUMPUNCT, OPT_PUNCTPRO, OPT_ALNUMPUNCT, OPT_NUMPUNCTPRO, 
  OPT_KOCH, OPT_KOCH_ADAPTIVE
  };

enum PROMPT_TYPE { NO_PROMPT, CODE_ONLY, DISP_ONLY, CODE_AND_DISP };

enum GEN_TYPE { RANDOMS, WORDS, ABBREVS, MIXED, WORDS2, WORDS3, WORDS4, REALQSOS, KOCH_LEARN };              
int generatorMode = RANDOMS;  // the CW Gen mode selected

enum systemStateStates {
  START, CWKEYER, CWGENERATOR, ECHOTRAINER, KOCHTRAINER, SETTINGS, HELP, SHUTDOWN, ABOUT
};
systemStateStates systemState = START;
systemStateStates systemWasState = START;
int taskStep = 0;

boolean leftKey, rightKey;
boolean DIT_FIRST = false;  // first latched was dit?
unsigned int ditLength ;  // dit length in milliseconds - 100ms = 60bpm = 12 wpm
unsigned int dahLength ;  // dahs are 3 dits long

unsigned long interWordTimer = 0;  // timer to detect interword spaces
unsigned long interCharacterTimer = 0;  // timer to detect interword spaces
unsigned int interCharacterSpace;  // need to be properly initialised
unsigned int interWordSpace;  // need to be properly initialised
unsigned long acsTimer = 0;  // timer for automatic character spacing (ACS)
unsigned long genTimer;  // timer used for generating morse code in trainer mode
unsigned int halfICS;  // used for word doubling: half of extra ICS
unsigned int effWpm;  // calculated effective speed in WpM
unsigned long kochCWTime;  // timer used for generating koch CW gen chars

uint16_t C;  // built binary version of coded character

String echoResponse = "";

enum echoStates { START_ECHO, SEND_WORD, REPEAT_WORD, GET_ANSWER, COMPLETE_ANSWER, EVAL_ANSWER };
echoStates echoTrainerState = START_ECHO;
String echoTrainerPrompt, echoTrainerWord;

enum AutoStopModes { nextword, halt, repeatword }; 
AutoStopModes autoStop = halt;

enum genStates {KEY_DOWN, KEY_UP };   
genStates generatorState;

String CWword = "";
String clearText = "";
int repeats = 0;
boolean active = false;  // flag for generator mode
boolean startFirst = true;  // showstarting a new sequence in the generator mode
boolean firstTime = true;  // for word doubler mode
uint8_t wordCounter = 0;  // for maxSequence
uint16_t errCounter = 0;  // counting errors in echo trainer mode
boolean stopFlag = false;  // for maxSequence
boolean echoStop = false;  // for maxSequence
boolean alternatePitch = false;  // change pitch in CW generator / file player
boolean startWords1 = true;
boolean startWords2 = true;
boolean startWords3 = true;
boolean startWords4 = true;

// Koch lesson sequences
const String KochChars = "KMRSUAPTLOWI.NJEF0Y,VG5/Q9ZH38B?427C1D6X-=k+snaeb@:";
const String LCWOChars = "KMURESNAPTLWI.JZ=FOY,VG5/Q92H38B?47C1D60X-k+asneb@:";
const String CWACChars = "TEANOIS14RHDL25UCMW36?FYPG79/BVKJ80=XQZ.,-k+asneb@:";
const String LICWChars = "REATINPGSLCDHOFUWBKMY59,QXV73?+k=16.ZJ/28b40-asne@:";
//                        0....5....1....5....2....5....3....5....4....5....5....5
 

const String CWchars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890.,:-/=?@+$!sankeb";
//                      0....5....1....5....2....5....3....5....4....5....5....5

char morse[256];
int morseA[256];

void init_morse() {
  for (int i=0; i<256; i++) { morse[i] = '*'; morseA[i] = 0; }
  // letters
  morse[0x0005] = 'A'; morseA['A'] = 0x0005;  // .-        
  morse[0x0018] = 'B'; morseA['B'] = 0x0018;  // -...
  morse[0x001a] = 'C'; morseA['C'] = 0x001a;  // -.-.
  morse[0x000c] = 'D'; morseA['D'] = 0x000c;  // -..
  morse[0x0002] = 'E'; morseA['E'] = 0x0002;  // .
  morse[0x0012] = 'F'; morseA['F'] = 0x0012;  // ..-.
  morse[0x000e] = 'G'; morseA['G'] = 0x000e;  // --.
  morse[0x0010] = 'H'; morseA['H'] = 0x0010;  // ....
  morse[0x0004] = 'I'; morseA['I'] = 0x0004;  // ..
  morse[0x0017] = 'J'; morseA['J'] = 0x0017;  // .---
  morse[0x000d] = 'K'; morseA['K'] = 0x000d;  // -.-
  morse[0x0014] = 'L'; morseA['L'] = 0x0014;  // .-..
  morse[0x0007] = 'M'; morseA['M'] = 0x0007;  // --
  morse[0x0006] = 'N'; morseA['N'] = 0x0006;  // -.
  morse[0x000f] = 'O'; morseA['O'] = 0x000f;  // ---
  morse[0x0016] = 'P'; morseA['P'] = 0x0016;  // .--.
  morse[0x001d] = 'Q'; morseA['Q'] = 0x001d;  // --.-
  morse[0x000a] = 'R'; morseA['R'] = 0x000a;  // .-.
  morse[0x0008] = 'S'; morseA['S'] = 0x0008;  // ...
  morse[0x0003] = 'T'; morseA['T'] = 0x0003;  // -
  morse[0x0009] = 'U'; morseA['U'] = 0x0009;  // ..-
  morse[0x0011] = 'V'; morseA['V'] = 0x0011;  // ...-
  morse[0x000b] = 'W'; morseA['W'] = 0x000b;  // .--
  morse[0x0019] = 'X'; morseA['X'] = 0x0019;  // -..-
  morse[0x001b] = 'Y'; morseA['Y'] = 0x001b;  // -.--
  morse[0x001c] = 'Z'; morseA['Z'] = 0x001c;  // --..         
  // numbers
  morse[0x002f] = '1'; morseA['1'] = 0x002f;  // .----
  morse[0x0027] = '2'; morseA['2'] = 0x0027;  // ..---
  morse[0x0023] = '3'; morseA['3'] = 0x0023;  // ...--
  morse[0x0021] = '4'; morseA['4'] = 0x0021;  // ....-
  morse[0x0020] = '5'; morseA['5'] = 0x0020;  // .....
  morse[0x0030] = '6'; morseA['6'] = 0x0030;  // -....
  morse[0x0038] = '7'; morseA['7'] = 0x0038;  // --...
  morse[0x003c] = '8'; morseA['8'] = 0x003c;  // ---..
  morse[0x003e] = '9'; morseA['9'] = 0x003e;  // ----.
  morse[0x003f] = '0'; morseA['0'] = 0x003f;  // -----
  // punctuation
  morse[0x005e] = '\''; morseA['\''] = 0x005e;  // ' .----.
  //morse[0x0028] = '&'; morseA['&'] = 0x0028;  // & .-... also is <as>
  morse[0x005a] = '@'; morseA['@'] = 0x005a;  // @ .--.-.
  morse[0x006d] = ')'; morseA[')'] = 0x006d;  // ) -.--.-
  //morse[0x0036] = '('; morseA['('] = 0x0036;  // ( -.--. also is <kn>
  morse[0x0078] = ':'; morseA[':'] = 0x0078;  // : ---...
  morse[0x0073] = ','; morseA[','] = 0x0073;  // , --..--
  morse[0x0089] = '$'; morseA['$'] = 0x0089;  // $ ...-..-
  morse[0x0031] = '='; morseA['='] = 0x0031;  // = -...-
  morse[0x006b] = '!'; morseA['!'] = 0x006b;  // ! -.-.--
  morse[0x0055] = '.'; morseA['.'] = 0x0055;  // .-.-.-
  morse[0x0061] = '-'; morseA['-'] = 0x0061;  // -....-
  morse[0x002a] = '+'; morseA['+'] = 0x002a;  // + .-.-.
  morse[0x0052] = '"'; morseA['"'] = 0x0052;  // " .-..-.
  morse[0x004c] = '?'; morseA['?'] = 0x004c;  // ? ..--..
  morse[0x006a] = ';'; morseA[';'] = 0x006a;  // ; -.-.-.
  morse[0x0032] = '/'; morseA['/'] = 0x0032;  // / -..-.
  //morse[0x0013] = ' '; morseA[' '] = 0x0013;  // ..--
  // prosigns
  morse[0x0028] = 's'; morseA['s'] = 0x0028;  // <as> .-... means wait
  morse[0x0035] = 'a'; morseA['a'] = 0x0035;  // <ka> -.-.- means attention
  morse[0x0036] = 'n'; morseA['n'] = 0x0036;  // <kn> -.--. means invite named station to xmit
  morse[0x0045] = 'k'; morseA['k'] = 0x0045;  // <sk> ...-.- means over and out
  morse[0x0022] = 'e'; morseA['e'] = 0x0022;  // <ve> ...-. means verified
  morse[0x00c5] = 'b'; morseA['b'] = 0x00c5;  // <bk> -...-.- means break in

  updateTimings();  // sets ditLength, dahLength and other timing values      
}
//  end morse stuff

/*******************************************************/

//  scroll lists

String toolsMenu[] = {"CW Keyer", "CW Generator", "Echo Trainer",
    "Koch Trainer", "Settings", "Help", "Shutdown", "About" };  // 8 lines

String start1[] = {"Start", "Settings", "Help" };  // 3 lines

String start2[] = {"Choose Word Source", "Start", "Settings", "Help" };  // 4 lines

String start3[] = {"Choose Lesson", "Start Koch CW Gen", 
    "Start Koch Echo Trn", "Settings", "Help" };  // 5 lines

int startItemSelected = 1;  // for keeping track of menu item selected

String sourceMenu[] = {"Random Chars", "English Words",
     "Abbreviations", "Mixed from above",
    "2 Word Phrases", "3 Word Phrases",
    "4 Word Phrases", "Real QSOs" };  // 8 lines

String kochOrderMenu[] = {"Koch Order", "LCWO Order",
     "CW Academy Order", "LICW Order" };  // 4 lines

String HelpScrollButtons[] = {"Short press up/down",
                             "to scroll. Long press",
                             "up select item. Long",
                             "press down cancel or",
                             "exit. Otherwise any",
                             "button to dismiss.", 
                             "Turn off scroll help",
                             "notice in settings."};  // 8 lines

String HelpCWKeyer[] = { "CWKeyer is used to",
                         "practice keying.",
                         "There are many",
                         "options available",
                         "in Settings.",
                         "Use buttons to",
                         "pause or exit." };  // 7 lines

String HelpCWGenerator[] = { "CWGenerator will",
                             "generate chars or",
                             "words from many",
                             "sources for copy.",
                             "Use the keyer to",
                             "pause or repeat",
                             "output.",
                             "There are many",
                             "options available",
                             "in Settings.",
                             "Use buttons to",
                             "pause or exit." };  // 12 lines

String HelpEchoTrainer[] = { "EchoTrainer will",
                             "generate a word",
                             "from many sources",
                             "then wait for the",
                             "keyer to repeat",
                             "it back.",
                             "There are many",
                             "options available",
                             "in Settings.",
                             "Use buttons to",
                             "pause or exit." };  // 11 lines

String HelpKochTrainer[] = { "KochTrainer will",
                             "teach Morse using",
                             "the Koch method.",
                             "Characters can be",
                             "generated or",
                             "echoed back.",
                             "Use a key to",
                             "continue after a",
                             "generated group.",
                             "Use buttons to",
                             "pause or exit." };  // 11 lines

String HelpTool[] = { "CWKeyer is used to",
                      "practice keying.",  
                      "CWGenerator will",
                      "generate chars or",
                      "words from many",
                      "sources for copy.",
                      "EchoTrainer will",
                      "generate a word",
                      "from many sources",
                      "then wait for the",
                      "keyer to repeat",
                      "it back.",
                      "KochTrainer will",
                      "teach Morse using",
                      "the Koch method.",
                      "There are tons of",
                      "options available",
                      "in Settings.",
                      "Use buttons to",
                      "pause or exit." };  // 20 lines

String toolAbout[] = {"PRACTICE MORSE", "              " + VERSION, "Edward J Williams" };  // 3 lines


String *scrollList = new String[10];  // prime scrollList
int scrollListItemCount;
int scrollListItemSelected;
String scrollListValueSelected;
int scrollHighLight = 1;  // 0 - scroll item highlighted, 1 - no highlight 

void create_scroll_list(String list[], int selected);

// for SPIFFS file handling
File file;
int fileNumber = 0;  // 0 - all closed, 1 - RealQSOs, 2 - 2 word phrases
                     // 3 - 3 word phrases, 4 - 4 word phrases
uint32_t fileWordPointer;  // where in the file we are, temp - not saved                     


/********************************************************/                    
void setup() {
  Serial.begin(115200);
  while(!Serial);  // wait for serial interface to start
  Serial.println("\nBooting");
  
  // setup battery pin
  pinMode(BATTERY_PIN, OUTPUT);
  digitalWrite(BATTERY_PIN, HIGH);

  // setup input pins for paddle dash and dot
  // they will be pulled high and grounded when the paddles are pressed
  rightPin = RIGHT_PIN;
  leftPin = LEFT_PIN;
  pinMode(rightPin, INPUT_PULLUP);  // GPIO12 is dash input
  pinMode(leftPin, INPUT_PULLUP);  // GPIO13 is dot input

  // load preferences
  loadPreferences();
  
  // initialize display  
  tft.begin();
  tft.setRotation(1);
  tft.setTextColor(TFT_WHITE,TFT_BLACK); 
  tft.fillScreen(TFT_BLACK);
  tft.setSwapBytes(true);
  tft.setTextFont(4);  // always assume font 4
  TJpgDec.setJpgScale(2);  // set jpg at 50% size, use 1 for full size
  TJpgDec.setCallback(tft_output);
  
  ScreenBlock = (uint16_t *) malloc(240 * 106);  // for four line block of screen
  tft.readRect(0, 31, 240, 104, ScreenBlock);  // save a blank screen

  // initialize SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS initialization failed!");
    tft.setTextDatum(TC_DATUM);  // MR_DATUM 5 - sets text location to middle right
    tft.drawString("SPIFFS Failed", 120, 40, 4);
    tft.drawString("Get Help", 120, 80, 4);
    while (1) yield(); // Stay here twiddling thumbs waiting
  }

  // show startup splash screen
  tft.setTextDatum(TC_DATUM);  // MR_DATUM 5 - sets text location to middle right
    // TL_DATUM 0 - is top left (default), MC_DATUM 4 - middle center
  tft.drawString("PRACTICE MORSE", 120, 40, 4);
  tft.drawString(VERSION, 120, 80, 4);
  tft.setTextDatum(TL_DATUM);

  update_battery_info();

  init_morse();

  // initialize buttons
  button_init();
  //delay(1000);  // need a delay here if there is no delay following

  // 3 second delay to show splash screen
  for (int i = 0; i < 6; i++) {
    delay(500);  
    update_battery_info();
  }

  tft.pushRect(0, 31, 240, 104, ScreenBlock);  // blank screen

  // setup tone stuff
  ledcAttachPin(TONE_OUTPUT_PIN, TONE_PWM_CHANNEL);
  ledcSetup(TONE_PWM_CHANNEL, notes[toneFreq], LEDC_TIMER_12_BIT);
  // prime the tone generator
  soundSignalOK();

  Serial.println("\nPractice Morse " + VERSION + " Edward J Williams");
}


unsigned long activityTimeout = 0;

void loop() {

  switch (keyerState) {  // check paddle state
    case DIT:
    case DAH:
    case KEY_START: 
      break;
    default: checkPaddles();
      break;
  }

  if (buttonUsage == IDLE) {
    switch (systemState) {
      case START:
        process_START();
        break;
      case CWKEYER:
        process_CWKEYER();
        break;
      case CWGENERATOR:
        process_CWGENERATOR();
        break;
      case ECHOTRAINER:
        process_ECHOTRAINER();
        break;
      case KOCHTRAINER:
        process_KOCHTRAINER();
        break;
      case SETTINGS:
        process_SETTINGS();
        break;
      case HELP:
        process_HELP();
        break;
      case SHUTDOWN:
        process_SHUTDOWN();
        break;
      case ABOUT:
        process_ABOUT();
        break;
      default:
          displayLine(1, "State Error", 0);
        break;
    }
  }

  // check if a button was pressed
  button_loop();

  // sleep if no activity for 10 minutes
  if ( leftKey || rightKey ) {  
    activityTimeout = millis();
  } else {
    if (millis() - activityTimeout > 600000) {  // 10 * 60 * 1000
      process_SHUTDOWN();
    }
  }
  
  // update battery status
  if ( millis() - timeout_battery > 500 ) {
    update_battery_info();
    timeout_battery = millis();
  }
}


/******************************************************************/
// functions...

//  battery stuff
void update_battery_info() {
    batteryVLevel = BL.getBatteryVolts();

    if ((batteryVLevel <= MIN_USB_VOL_NO_CHARGE) && (batteryVLevel >= MIN_USB_VOL_CHARGE)) {
      // show charging icon(s) when charging
      batteryLevelPrev = 101;
      imgNumPrev = 6;
      drawingBatteryText("Chrg");
      switch (batteryCheckState) {
        case 10: 
          batteryCheckState = 11;
          break;
        case 11: 
          batteryCheckState = 12;
          break;
        case 12: 
          batteryCheckState = 13;
          break;
        case 13: 
          batteryCheckState = 14;
          break;
        default: 
          batteryCheckState = 10;
          break;
      }
      drawingBatteryIcon(batteryImages[(batteryCheckState-10)]);
    }else{
      // show appropriate icon image for charge level
      int batteryLevel = BL.getBatteryChargeLevel();
      batteryLevel = min(batteryLevelPrev,batteryLevel);
      
      if (batteryLevel <= 0) {  // battery too low - shutdown
        tft.fillScreen(TFT_BLACK);
        drawingBatteryIcon(batteryImages[0]);
        drawingBatteryText(String(batteryLevel) + "%");
        tft.setTextDatum(TC_DATUM);  // MR_DATUM 5 - sets text location to middle right
        tft.drawString("Power off/on to restart", 120, 120, 2);
        for (int i=0; i<10; i++) {
          tft.drawString("BATTERY TOO LOW", 120, 40, 4);
          tft.drawString("SHUTTING DOWN", 120, 80, 4);
          delay(500);
          tft.pushRect(0, 31, 240, 104, ScreenBlock);  // blank screen
          tft.drawString("Power off/on to restart", 120, 120, 2);
          delay(500);
        }            
        esp_deep_sleep_start();
      }

      if(batteryLevel >= 80){
        batteryCheckState = 3;
      }else if(batteryLevel < 80 && batteryLevel >= 50 ){
        batteryCheckState = 2;
      }else if(batteryLevel < 50 && batteryLevel >= 20 ){
        batteryCheckState = 1;
      }else if(batteryLevel <= 10){
        if (batteryCheckState == 0) {
          batteryCheckState = 20;
        }else{
          batteryCheckState = 0;
        }
      }else {  // batteryLevel < 20  
        batteryCheckState = 0;
      }  
    
      if(batteryLevelPrev != batteryLevel){  // show new charge level if changed
        drawingBatteryText(String(batteryLevel) + "%");
        batteryLevelPrev = batteryLevel;
      }
      if(batteryLevel <= 10){  // flash battery icon if too low
        if (batteryCheckState == 0) {  // hide battery icon
          tft.fillRect(BATTERY_ICON_POS_X, 0, BATTERY_ICON_WIDTH, BATTERY_ICON_HEIGHT, TFT_BLACK);
        }else{  // show battery icon
          drawingBatteryIcon(batteryImages[0]);
        }
      } else {
        if(imgNumPrev != batteryCheckState){  // show new icon if changed
          drawingBatteryIcon(batteryImages[batteryCheckState]);
          imgNumPrev = batteryCheckState;    
        }
      }
    }
      //tft.print("Never Used Stack Size: ");
      //tft.println(uxTaskGetStackHighWaterMark(NULL));
}

void drawingBatteryIcon(String filePath){
   TJpgDec.drawFsJpg(BATTERY_ICON_POS_X, 0, filePath);
}

void drawingBatteryText(String text){
  int CurXB = tft.getCursorX(); // save current cursor stuff
  int CurYB = tft.getCursorY();
  tft.setTextDatum(5);  // MR_DATUM 5 - sets text location to middle right
                        // TL_DATUM 0 - is top left (default)
  tft.fillRect(BATTERY_TEXT_POS_X, 0, BATTERY_TEXT_WIDTH, BATTERY_TEXT_HEIGHT, TFT_BLACK);
  tft.drawString(text, BATTERY_ICON_POS_X-2, BATTERY_TEXT_HEIGHT/2, 2);
  tft.setTextDatum(0);  // MR_DATUM 5 - sets text location to middle right
  tft.setCursor(CurXB, CurYB, 4);  // restore current cursor stuff (always assume font 4
}

bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap)
{
  if ( y >= tft.height() ) return 0;
  tft.pushImage(x, y, w, h, bitmap);
  return 1;
}
//  end battery stuff

/**********************************************************/

//  button stuff
void button_init() {

    btnUp.setClickHandler([](Button2 & b) {  // Up short press
      if (buttonUsage == SCROLL) {  // scrolling
        scroll_up();
      }
      buttonPressed = UPSHORT;
      activityTimeout = millis();      
    });
    btnUp.setLongClickHandler([](Button2 & b) {  // Up long press
      unsigned int time = b.wasPressedFor();
      if (time >= 1000) {  // check if really was a long click
        // nothing
      } else {
        if (buttonUsage == SCROLL) {  // scrolling
          select_scroll_item();
          buttonUsage = IDLE;
        } 
      }
      buttonPressed = UPLONG;
      activityTimeout = millis();      
    });
    btnDwn.setClickHandler([](Button2 & b) {  // Down short press
      if (buttonUsage == SCROLL) {  // scrolling
        scroll_down();
      }
      buttonPressed = DOWNSHORT;
      activityTimeout = millis();      
    });
    btnDwn.setLongClickHandler([](Button2 & b) {  // Down long press
      unsigned int time = b.wasPressedFor();
      if (time >= 1000) {  // check for very long press
        // nothing
      } else {
        if (buttonUsage == SCROLL) {  // scrolling
          select_scroll_item();
          scrollListValueSelected = "";
          buttonUsage = IDLE;
        } 
      }
      buttonPressed = DOWNLONG;
      activityTimeout = millis();      
    });
}

void button_loop() {
  
    // Check for button presses
    btnUp.loop();
    btnDwn.loop();
}
//  end button stuff


/**************************************************************/

//  display stuff
void blankLine(int line) {
  switch (line) {
    case 0:  // blank all lines
      tft.fillRect(0, 0, 170, 26, TFT_BLACK);
      tft.fillRect(0, 31, 240, 109, TFT_BLACK);
      tft.setCursor(0, 0);
      break;
    case 1:  
      tft.fillRect(0, 0, 170, 26, TFT_BLACK);
      tft.setCursor(0, 0);
      break;
    case 2:    
      tft.fillRect(0, 31, 240, 26, TFT_BLACK);
      tft.setCursor(0,31);
      break;
    case 3:    
      tft.fillRect(0, 57, 240, 26, TFT_BLACK);
      tft.setCursor(0,57);
      break;
    case 4:    
      tft.fillRect(0, 83, 240, 26, TFT_BLACK);
      tft.setCursor(0,83);
      break;
    default:    
      tft.fillRect(0, 109, 240, 26, TFT_BLACK);
      tft.setCursor(0,109);
      break;
  }
}

void displayLine(int line, String text, int highlight) {  
  // if highlight is 1, display text black on white
  blankLine(line);
  if (highlight) {
    tft.setTextColor(TFT_BLACK,TFT_WHITE); 
  }
  switch (line) {
    case 1:  
      tft.drawString(text, 0, 0);
      tft.setCursor(0, 0);
      break;
    case 2:    
      tft.drawString(text, 0, 31);
      tft.setCursor(0,31);
      break;
    case 3:    
      tft.drawString(text, 0, 57);
      tft.setCursor(0,57);
      break;
    case 4:    
      tft.drawString(text, 0, 83);
      tft.setCursor(0,83);
      break;
    default:    
      tft.drawString(text, 0, 109);
      tft.setCursor(0,109);
      break;
  }    
  if (highlight) {
    tft.setTextColor(TFT_WHITE,TFT_BLACK); 
  }
}

// display a scroll list on lines 3-5 and return selection
void create_scroll_list(String list[], int listlength, int selected) {
  // load scroll list
  // display scroll list with selected item highlighted and visible
  //   on top if first, on bottom if last, else selected in middle

  blankLine(3);
  blankLine(4);
  blankLine(5);
  buttonPressed = NONE;

  delete[] scrollList;                  // resize scollList
  scrollList = new String[listlength];  // as needed

  scrollListValueSelected = "";
  scrollListItemCount = listlength;

  for (int i; i<scrollListItemCount; i++) {
    scrollList[i] = list[i];
  }
  scrollListItemSelected = selected;
  if (scrollListItemSelected < 1) { 
    scrollListItemSelected = 1; 
  }
  if (scrollListItemSelected > scrollListItemCount) { 
    scrollListItemSelected = scrollListItemCount; 
  }
  buttonUsage = SCROLL;  // set to 1 to use buttons for scrolling
  display_scroll_list();
}

// display current visible scroll items
void display_scroll_list() {
  if (buttonUsage == SCROLL) {
    // show first visible scroll item on line 3
    if (scrollHighLight == 0 && scrollListItemSelected == 1) {
      scrollListItemSelected = 2;  // scrolling without highlights
    }
    if (scrollListItemSelected == 1) {
      displayLine(3, scrollList[0], scrollHighLight);  // highlight line
    } else {
      if (scrollListItemSelected == scrollListItemCount && scrollListItemCount > 2) {
        displayLine(3, scrollList[scrollListItemSelected-3], 0);
      }else{
        displayLine(3, scrollList[scrollListItemSelected-2], 0);
      }
    }
    // show second visible scroll item on line 4
    if (scrollListItemCount > 1) {
      if (scrollListItemSelected == 1) {
        displayLine(4, scrollList[1], 0);
      }else{
        if (scrollListItemSelected == scrollListItemCount) {
          if (scrollListItemCount == 2) {
            displayLine(4, scrollList[scrollListItemSelected-1], scrollHighLight);  // highlight line
          }else{
            displayLine(4, scrollList[scrollListItemSelected-2], 0);
          }
        }else{
          displayLine(4, scrollList[scrollListItemSelected-1], scrollHighLight);  // lightlight line
        }
      }
    }
    // show third visible scroll item on line 5
    if (scrollHighLight == 0 && scrollListItemSelected == scrollListItemCount) {
      scrollListItemSelected = scrollListItemCount - 1;  // scrolling without highlights
    }
    if (scrollListItemCount > 2) {
      if (scrollListItemSelected == 1) {
        displayLine(5, scrollList[scrollListItemSelected+1], 0);
      }else{
        if (scrollListItemSelected == scrollListItemCount) {
          displayLine(5, scrollList[scrollListItemSelected-1], scrollHighLight);  // highlight line
        } else {
          displayLine(5, scrollList[scrollListItemSelected], 0);
        }
      }
    }
    // show scroll arrows
    // up arrow
    tft.drawLine(230, 62, 234, 58, TFT_WHITE);
    tft.drawLine(230, 63, 234, 59, TFT_WHITE);
    tft.drawLine(230, 64, 234, 60, TFT_WHITE);
    tft.drawLine(234, 58, 238, 62, TFT_WHITE);
    tft.drawLine(234, 59, 238, 63, TFT_WHITE);
    tft.drawLine(234, 60, 238, 64, TFT_WHITE);
    // down arrow
    int Y = 128;
    if (scrollListItemCount == 2) {
      Y = Y - 26;
    }
    tft.drawLine(230, Y, 234, Y+4, TFT_WHITE);
    tft.drawLine(230, Y+1, 234, Y+5, TFT_WHITE);
    tft.drawLine(230, Y+2, 234, Y+6, TFT_WHITE);
    tft.drawLine(234, Y+4, 238, Y, TFT_WHITE);
    tft.drawLine(234, Y+5, 238, Y+1, TFT_WHITE);
    tft.drawLine(234, Y+6, 238, Y+2, TFT_WHITE);
    // bottom of top arrow 64, top of bottom arrow 128
    // position of scroll
    int scrollheight = Y - 65;
    int scrollboxheight = round(scrollheight / scrollListItemCount);
    if (scrollHighLight == 0) {
      if (scrollListItemCount < 4) {
        scrollboxheight = scrollheight-2;
      }else{
        scrollboxheight = round(scrollheight / (scrollListItemCount - 2));
      }
    }
    int scrollboxY = 65 + ((scrollListItemSelected - 1) * scrollboxheight);
    if (scrollHighLight == 0 && scrollListItemCount > 2) {
      scrollboxY = scrollboxY - scrollboxheight;
    }
    tft.fillRect(230, scrollboxY, 8, scrollboxheight, TFT_WHITE);
  }
}

// short click top button scrolls up
void scroll_up() {
  if (scrollListItemSelected > 1) {
    scrollListItemSelected--;    
  }
  display_scroll_list();
}

// short click bottom button scrolls down
void scroll_down() {
  if (scrollListItemSelected < scrollListItemCount) {
    scrollListItemSelected++;    
  }
  display_scroll_list();
}

// medium click top or bottom button returns selected scroll list item index
void select_scroll_item() {
  buttonUsage = IDLE;  // disable using buttons for scrolling
  // remove scroll list
  blankLine(3);
  blankLine(4);
  blankLine(5);
  scrollHighLight = 1;

  scrollListValueSelected = scrollList[scrollListItemSelected-1];
}

void checkShiftUp(int checkX) {  // checkX is typically 210
  if ((tft.getCursorX() > checkX) && (tft.getCursorY() > 108)) {
    tft.readRect(0, 57, 240, 78, ScreenBlock);
    tft.pushRect(0, 31, 240, 78, ScreenBlock);
    blankLine(5);
    tft.setCursor(0, 109);
  }
}

//  end display stuff


//  tasks

void process_START() {
  switch (taskStep) {
    case 0:  // show button/scroll help
      if (showScrollHelp) {
        displayLine(2, "Button/Scroll Help:", 0);     
        scrollHighLight = 0;
        create_scroll_list(HelpScrollButtons, 8, 1);
      }
      taskStep = 1;
      break;
    case 1:  // show top level tools menu
      displayLine(1, "Practice Morse", 0);
      displayLine(2, "Choose Tool:", 0);
      create_scroll_list(toolsMenu, 8, systemWasState);
      taskStep = 2;
      break;
    case 2:  // process selected tool
      if (buttonPressed == UPLONG) {
        buttonPressed = NONE;
        displayLine(4, "Selected:", 0);
        displayLine(5, scrollListValueSelected, 0);
        switch (scrollListItemSelected) {
          case 1:
            systemState = CWKEYER;
            break;
          case 2:
            systemState = CWGENERATOR;
            break;
          case 3:
            systemState = ECHOTRAINER;
            break;
          case 4:
            systemState = KOCHTRAINER;
            break;
          case 5:
            systemState = SETTINGS;
            break;
          case 6:
            systemState = HELP;
            break;
          case 7:
            systemState = SHUTDOWN;
            break;
          case 8:
            systemState = ABOUT;
            break;
          default:
            displayLine(1, "Menu Error", 0);
            break;
        }
        taskStep = 0;
      } else if (buttonPressed == DOWNLONG) {
        buttonPressed = NONE;
        blankLine(2);
        displayLine(3, "        Go To Sleep?", 0);
        displayLine(4, "Top button cancel", 0);
        displayLine(5, "Bottom button sleep", 0);
        taskStep = 4;
      }
      break;
    case 4:  // resume/exit Practice Morse
      if (buttonPressed == UPSHORT || buttonPressed == UPLONG) {  // continue
        buttonPressed = NONE;
        taskStep = 1;
      }
      if (buttonPressed == DOWNSHORT || buttonPressed == DOWNLONG) {  // exiting
        buttonPressed = NONE;
        systemState = SHUTDOWN;
        taskStep = 0;
      }
      break;
    default:
      displayLine(4, "Step Error", 0);
      break;
  }
}


void process_CWKEYER() {
  switch (taskStep) {
    case 0:  // show CW Keyer top menu
      if (systemWasState != systemState) {
        systemWasState = systemState;
        startItemSelected = 1;
        Serial.println("CWKEYER");
      }
      blankLine(0); 
      displayLine(1, "CW Keyer", 0);
      create_scroll_list(start1, 3, startItemSelected);
      taskStep = 1;
      break;
    case 1:  // process selected option
      if (buttonPressed == UPLONG) {
        buttonPressed = NONE;
        switch (scrollListItemSelected) {
          case 1:
            startItemSelected = 1;
            startNotice(2);
            taskStep = 2;
            break;
          case 2:
            startItemSelected = 2;
            displayLine(2, "Settings", 0);
            processPref(true);  // process prefs first time
            taskStep = 11;
            break;
          case 3:
            startItemSelected = 3;
            scrollHighLight = 0;
            create_scroll_list(HelpCWKeyer, 7, 1);
            taskStep = 0;
            break;
          default:
            displayLine(1, "Menu Error", 0);
            break;
        }
      }
      if (buttonPressed == DOWNLONG) {
        buttonPressed = NONE;
        systemState = START;
        taskStep = 1;
      }
      break;
    case 2:  // do CW Keyer
      if (buttonPressed != NONE) {
        buttonPressed = NONE;
        taskStep = 3;
      }
      checkShiftUp(210);  // shift up if last line and X is at 210
      if (doPaddleIambic(leftKey, rightKey)) {
        return;  // we are busy keying and so need a very tight loop !
      }
      break;
    case 3:  // pause CW Keyer
      // save current screen
      CurX = tft.getCursorX();
      CurY = tft.getCursorY();
      tft.readRect(0, 31, 240, 109, ScreenBlock);

      blankLine(2);
      displayLine(3, "            PAUSED", 0);
      displayLine(4, "Top button continue", 0);
      displayLine(5, "Bottom button exit", 0);
      taskStep = 4;
      break;
    case 4:  // resume/exit CW Keyer
      if (buttonPressed == UPSHORT || buttonPressed == UPLONG) {  // continue
        buttonPressed = NONE;
        tft.pushRect(0, 31, 240, 109, ScreenBlock);
        tft.setCursor(CurX, CurY);
        taskStep = 2;
      }
      if (buttonPressed == DOWNSHORT || buttonPressed == DOWNLONG) {  // exiting
        Serial.println("");
        buttonPressed = NONE;
        systemState = CWKEYER;
        taskStep = 0;
      }
      break;
    case 11:  // process Settings
      if (buttonPressed != NONE) {
        processPref(false);
      }
      break;
    default:  // go back to cw keyer menu
      if (buttonPressed != NONE) {
        buttonPressed = NONE;
        taskStep = 0;
      }
      break;
  }
}


void process_CWGENERATOR() {
  switch (taskStep) {
    case 0:  // show CW Generator top menu
      startWords1 = true;
      startWords2 = true;
      startWords3 = true;
      startWords4 = true;
      alternatePitch = false;
      if (systemWasState != systemState) {
        systemWasState = systemState;
        startItemSelected = 1;
        generatorMode = saveGenMode;
        Serial.println("CWGENERATOR");
      }
      blankLine(0); 
      displayLine(1, "CW Generator", 0);
      if (generatorMode == 3) {
        displayLine(2, "Using Mixed Chars", 0);
      } else {
        displayLine(2, "Using " + sourceMenu[generatorMode], 0);
      }
      create_scroll_list(start2, 4, startItemSelected);
      taskStep = 1;
      break;
    case 1:  // process selected option
      if (buttonPressed == UPLONG) {
        buttonPressed = NONE;
        switch (scrollListItemSelected) {
          case 1:  // choose word source 
            startItemSelected = 1;
            create_scroll_list(sourceMenu, 8, generatorMode+1);
            taskStep = 2;
            break;
          case 2:  // Start
            startItemSelected = 2;
            startNotice(3);
            delay(1000);
            cleanStartSettings();
            active = true;
            taskStep = 3;
            break;
          case 3:
            startItemSelected = 3;
            displayLine(2, "Settings", 0);
            processPref(true);  // process prefs first time
            taskStep = 11;
            break;
          case 4:
            startItemSelected = 4;
            blankLine(2); 
            scrollHighLight = 0;
            create_scroll_list(HelpCWGenerator, 12, 1);
            taskStep = 0;
            break;
          default:
            displayLine(4, "Selection", 0);
            displayLine(5, "not implemented yet", 0);
            break;
        }
      }
      if (buttonPressed == DOWNLONG) {
        buttonPressed = NONE;
        systemState = START;
        taskStep = 1;
      }
      break;
    case 2:  // process word source selection
      if (buttonPressed == UPLONG) {
        buttonPressed = NONE;
        generatorMode = scrollListItemSelected - 1;
        saveGenMode = generatorMode;
        savePreferences();
        taskStep = 0;
      } else if (buttonPressed == DOWNLONG) {
        buttonPressed = NONE;
        taskStep = 0;
      }
      break;
    case 3:
      if (buttonPressed != NONE) {  // go to pause
        buttonPressed = NONE;
        taskStep = 4;
      }
      if (autoStopMode) {  // if in autoStopMode
        if (autoStop == halt) {  
          if (leftKey) {
            autoStop = repeatword;
            delay(15);
          } else if (rightKey) {
            autoStop = nextword;
            delay(15);
          } else { break; }
          // for debouncing:
          while ( checkPaddles() )
              delay(15);  // wait until paddles are released
          active = true;
          break;
        }  // end of squeeze
      }  // end autostop
      if (leftKey || rightKey) {  // starting...
        // for debouncing:
        while ( checkPaddles() )
            delay(15);
        delay(15);
        active = !active;

        if (active) { 
          Serial.println(" continue ");
          cleanStartSettings();
        } else {  // pausing
          keyOut(false, notes[toneFreq], volume);
          Serial.println(" paused ");
        }
      } else {  // no paddle pressed - check stop flag
        checkStopFlag();
      }
      if (active) { 
        generateCW(); 
      }
      break;
    case 4:  // pause CW Generator
      // save current screen
      CurX = tft.getCursorX();
      CurY = tft.getCursorY();
      tft.readRect(0, 31, 240, 109, ScreenBlock);

      keyOut(false, notes[toneFreq], volume);
      blankLine(2);
      displayLine(3, "            PAUSED", 0);
      displayLine(4, "Top button continue", 0);
      displayLine(5, "Bottom button exit", 0);
      taskStep = 5;
      break;
    case 5:  // resume/exit CW Generator
      if (buttonPressed == UPSHORT || buttonPressed == UPLONG) {  // continue
        buttonPressed = NONE;
        tft.pushRect(0, 31, 240, 109, ScreenBlock);
        tft.setCursor(CurX, CurY);
        taskStep = 3;
      }
      if (buttonPressed == DOWNSHORT || buttonPressed == DOWNLONG) {  // exiting
        Serial.println("");
        buttonPressed = NONE;
        systemState = CWGENERATOR;
        taskStep = 0;
        saveWordPointers();
      }
      break;
    case 11:  // process Settings
      if (buttonPressed != NONE) {
        processPref(false);
      }
      break;
    default:  // go back to cw generator menu
      if (buttonPressed != NONE) {
        buttonPressed = NONE;
        taskStep = 0;
      }
      break;
  }
}


void process_ECHOTRAINER() {
  static int OldEchoTrainerState = 10;
  if (OldEchoTrainerState != echoTrainerState) {
    OldEchoTrainerState = echoTrainerState;
  }
  switch (taskStep) {
    case 0:  // show Echo Trainer top menu
      if (systemWasState != systemState) {
        systemWasState = systemState;
        startItemSelected = 1;
        generatorMode = saveGenMode;
        Serial.println("ECHOTRAINER");
      }
      blankLine(0); 
      displayLine(1, "Echo Trainer", 0);
      if (generatorMode == 3) {
        displayLine(2, "Using Mixed Chars", 0);
      } else {
        displayLine(2, "Using " + sourceMenu[generatorMode], 0);
      }
      create_scroll_list(start2, 4, startItemSelected);
      taskStep = 1;
      break;
    case 1:  // process selected option
      if (buttonPressed == UPLONG) {
        buttonPressed = NONE;
        switch (scrollListItemSelected) {
          case 1:  // choose word source 
            startItemSelected = 1;
            create_scroll_list(sourceMenu, 8, generatorMode+1);
            taskStep = 2;
            break;
          case 2:  // Start
            startItemSelected = 2;
            startNotice(3);
            cleanStartSettings();  // START_ECHO set in here
            active = true;
            taskStep = 3;
            break;
          case 3:
            startItemSelected = 3;
            displayLine(2, "Settings", 0);
            processPref(true);  // process prefs first time
            taskStep = 11;
            break;
          case 4:
            startItemSelected = 4;
            blankLine(2); 
            scrollHighLight = 0;
            create_scroll_list(HelpEchoTrainer, 11, 1);
            taskStep = 0;
            break;
          default:
            displayLine(4, "Selection", 0);
            displayLine(5, "not implemented yet", 0);
            break;
        }
      }
      if (buttonPressed == DOWNLONG) {
        buttonPressed = NONE;
        systemState = START;
        taskStep = 1;
      }
      break;
    case 2:  // process word source selection
      if (buttonPressed == UPLONG) {
        buttonPressed = NONE;
        generatorMode = scrollListItemSelected - 1;
        saveGenMode = generatorMode;
        savePreferences();
        taskStep = 0;
      } else if (buttonPressed == DOWNLONG) {
        buttonPressed = NONE;
        taskStep = 0;
      }
      break;
    case 3:  // start
      if (buttonPressed != NONE) {  // go to pause
        buttonPressed = NONE;
        taskStep = 4;
      }
      checkStopFlag();
      if (active) {
        switch (echoTrainerState) {
          case START_ECHO:   
            echoTrainerState = SEND_WORD;
            genTimer = millis() + interCharacterSpace + (promptPause * interWordSpace);
            break;
          case SEND_WORD:
          case REPEAT_WORD:  
            echoResponse = ""; 
            generateCW();
            break;
          case EVAL_ANSWER:  
            echoTrainerEval();
            break;
          case COMPLETE_ANSWER:                    
          case GET_ANSWER:   
            if (doPaddleIambic(leftKey, rightKey)) 
              return;  // we are busy keying and so need a very tight loop !
            break;
          default:
            break;
        }                              
      }
      break;
    case 4:  // pause Echo Trainer
      // save current screen
      CurX = tft.getCursorX();
      CurY = tft.getCursorY();
      tft.readRect(0, 31, 240, 109, ScreenBlock);

      keyOut(false, notes[toneFreq], volume);
      blankLine(2);
      displayLine(3, "            PAUSED", 0);
      displayLine(4, "Top button continue", 0);
      displayLine(5, "Bottom button exit", 0);
      taskStep = 5;
      break;
    case 5:  // resume/exit Echo Trainer
      if (buttonPressed == UPSHORT || buttonPressed == UPLONG) {  // continue
        buttonPressed = NONE;
        tft.pushRect(0, 31, 240, 109, ScreenBlock);
        tft.setCursor(CurX, CurY);
        taskStep = 3;
      }
      if (buttonPressed == DOWNSHORT || buttonPressed == DOWNLONG) {  // exiting
        Serial.println("");
        buttonPressed = NONE;
        systemState = ECHOTRAINER;
        taskStep = 0;
        saveWordPointers();
      }
      break;
    case 11:  // process Settings
      if (buttonPressed != NONE) {
        processPref(false);
      }
      break;
    default:  // go back to echo trainer menu
      if (buttonPressed != NONE) {
        buttonPressed = NONE;
        taskStep = 0;
      }
      break;
  }
}


void process_KOCHTRAINER() {
  static boolean firstShow;
  static uint8_t OrigLesson;
  static uint8_t NewLesson;
  switch (taskStep) {
    case 0:  // show Koch Trainer top menu
      alternatePitch = false;
      if (systemWasState != systemState) {
        systemWasState = systemState;
        startItemSelected = 1;
        Serial.println("KOCHTRAINER");
      }
      blankLine(0); 
      displayLine(1, "Koch Trainer", 0);
      switch (kochOrder) {
        case 0: displayLine(2, "Using Koch " + String(kochLesson) + " of 51", 0); break;
        case 1: displayLine(2, "Using LCWO " + String(lcwoLesson) + " of 51", 0); break;
        case 2: displayLine(2, "Using CWAC " + String(cwacLesson) + " of 51", 0); break;
        case 3: displayLine(2, "Using LICW " + String(licwLesson) + " of 51", 0); break;
      }
      create_scroll_list(start3, 5, startItemSelected);
      taskStep = 1;
      break;
    case 1:  // process selected option
      if (buttonPressed == UPLONG) {
        buttonPressed = NONE;
        switch (scrollListItemSelected) {
          case 1:  // choose lesson
            startItemSelected = 1;
            firstShow = true;
            switch (kochOrder) {
              case 0: { OrigLesson = kochLesson; break; }
              case 1: { OrigLesson = lcwoLesson; break; }
              case 2: { OrigLesson = cwacLesson; break; }
              case 3: { OrigLesson = licwLesson; break; }
            }
            NewLesson = OrigLesson;
            taskStep = 2;
            break;
          case 2:  // Start Koch CW Generator
            startItemSelected = 2;
            generatorMode = KOCH_LEARN;
            displayLine(3, String(kochCWTimer*10) + " seconds worth", 0);
            startNotice(4);
            delay(1000);
            cleanStartSettings();
            active = true;
            taskStep = 11;
            break;
          case 3:  // Start Koch Echo Trainer
            startItemSelected = 3;
            generatorMode = KOCH_LEARN;
            startNotice(3);
            delay(1000);
            cleanStartSettings();
            active = true;
            taskStep = 21;
            break;
          case 4:
            startItemSelected = 4;
            displayLine(2, "Settings", 0);
            processPref(true);  // process prefs first time
            taskStep = 31;
            break;
          case 5:
            startItemSelected = 5;
            blankLine(2); 
            scrollHighLight = 0;
            create_scroll_list(HelpKochTrainer, 11, 1);
            taskStep = 0;
            break;
          default:
            displayLine(4, "Selection", 0);
            displayLine(5, "not implemented yet", 0);
            break;
        }
      }
      if (buttonPressed == DOWNLONG) {
        buttonPressed = NONE;
        systemState = START;
        taskStep = 1;
      }
      break;
    case 2:  // process choose lesson
      switch (buttonPressed) {
        case UPSHORT:  // scroll up
          if (NewLesson < 51) { NewLesson++; }
          break;        
        case DOWNSHORT:  // scroll down
          if (NewLesson > 1) { NewLesson--; }
          break;
        case UPLONG:  // return keeping selected value
          switch (kochOrder) {
            case 0: { kochLesson = NewLesson; break; }
            case 1: { lcwoLesson = NewLesson; break; }
            case 2: { cwacLesson = NewLesson; break; }
            case 3: { licwLesson = NewLesson; break; }
          }
          displayLine(5, "    Change Saved", 0);
          delay(1000);
          savePreferences();
          taskStep = 0;
          break;
        case DOWNLONG:  // return keeping original value
          NewLesson = OrigLesson;
          displayLine(5, "  Change Not Saved", 0);
          delay(1000);
          taskStep = 0;
          break;
        default:
          break;
      }
      if (buttonPressed != NONE || firstShow) {
        buttonPressed = NONE;
        firstShow = false;
        switch (kochOrder) {
          case 0: { 
            displayLine(3, "Koch Lesson", 0); 
            displayLine(4, String(NewLesson) + " of 51 (" + 
              cleanUpProSigns(String(KochChars[NewLesson-1])) + ")", 1);
            break;
          }
          case 1: {
            displayLine(3, "LCWO Lesson", 0); 
            displayLine(4, String(NewLesson) + " of 51 (" + 
              cleanUpProSigns(String(LCWOChars[NewLesson-1])) + ")", 1);
            break;
          }
          case 2: {
            displayLine(3, "CW Academy Lesson", 0); 
            displayLine(4, String(NewLesson) + " of 51 (" + 
              cleanUpProSigns(String(CWACChars[NewLesson-1])) + ")", 1);
            break;
          }
          case 3: {
            displayLine(3, "LICW Lesson", 0); 
            displayLine(4, String(NewLesson) + " of 51 (" + 
              cleanUpProSigns(String(LICWChars[NewLesson-1])) + ")", 1);
            break;
          }
        }
        blankLine(5);
      }
      break;
    case 11:  // start koch cw gen
      if (buttonPressed != NONE) {  // go to pause
        buttonPressed = NONE;
        taskStep = 18;
      }
      if (leftKey || rightKey) {  // pause by key?
        static boolean pauseByKey = false;
        // for debouncing:
        while ( checkPaddles() )
            delay(15);
        delay(15);
        active = !active;

        if (active) { 
          Serial.print("continue ");
          cleanStartSettings();
          if (echoDisplay != CODE_ONLY && pauseByKey){
            displayGeneratedMorse(" > ");  // prompt between echo and response
          }
          pauseByKey = false;
        } else {  // pausing
          keyOut(false, notes[toneFreq], volume);
          Serial.print("pause ");
          pauseByKey = true;
        }
      }
      if (active) { 
        generateCW(); 
      }
      break;
    case 18:  // pause Koch Trainer
      // save current screen
      CurX = tft.getCursorX();
      CurY = tft.getCursorY();
      tft.readRect(0, 31, 240, 109, ScreenBlock);

      keyOut(false, notes[toneFreq], volume);
      blankLine(2);
      displayLine(3, "            PAUSED", 0);
      displayLine(4, "Top button continue", 0);
      displayLine(5, "Bottom button exit", 0);
      taskStep = 19;
      break;
    case 19:  // resume/exit Koch Trainer
      if (buttonPressed == UPSHORT || buttonPressed == UPLONG) {  // continue
        buttonPressed = NONE;
        tft.pushRect(0, 31, 240, 109, ScreenBlock);
        tft.setCursor(CurX, CurY);
        taskStep = 11;  
      }
      if (buttonPressed == DOWNSHORT || buttonPressed == DOWNLONG) {  // exiting
        Serial.println("");
        buttonPressed = NONE;
        systemState = KOCHTRAINER;
        taskStep = 0;
        saveWordPointers();
      }
      break;
    case 21:  // start koch echo trainer
      if (buttonPressed != NONE) {  // go to pause
        buttonPressed = NONE;
        taskStep = 28;
      }
      checkStopFlag();
      if (active) {
        switch (echoTrainerState) {
          case START_ECHO:   
            echoTrainerState = SEND_WORD;
            genTimer = millis() + interCharacterSpace + (promptPause * interWordSpace);
            break;
          case SEND_WORD:
          case REPEAT_WORD:  
            echoResponse = ""; 
            generateCW();
            break;
          case EVAL_ANSWER:  
            echoTrainerEval();
            break;
          case COMPLETE_ANSWER:                    
          case GET_ANSWER:   
            if (doPaddleIambic(leftKey, rightKey)) 
              return;  // we are busy keying and so need a very tight loop !
            break;
          default:
            break;
        }                              
      }
      break;
    case 28:  // pause Koch Trainer
      // save current screen
      CurX = tft.getCursorX();
      CurY = tft.getCursorY();
      tft.readRect(0, 31, 240, 109, ScreenBlock);

      keyOut(false, notes[toneFreq], volume);
      blankLine(2);
      displayLine(3, "            PAUSED", 0);
      displayLine(4, "Top button continue", 0);
      displayLine(5, "Bottom button exit", 0);
      taskStep = 29;
      break;
    case 29:  // resume/exit Koch Trainer
      if (buttonPressed == UPSHORT || buttonPressed == UPLONG) {  // continue
        buttonPressed = NONE;
        tft.pushRect(0, 31, 240, 109, ScreenBlock);
        tft.setCursor(CurX, CurY);
        taskStep = 21;  
      }
      if (buttonPressed == DOWNSHORT || buttonPressed == DOWNLONG) {  // exiting
        Serial.println("");
        buttonPressed = NONE;
        systemState = KOCHTRAINER;
        taskStep = 0;
        saveWordPointers();
      }
      break;
    case 31:  // process Settings
      if (buttonPressed != NONE) {
        processPref(false);
      }
      break;
    default:  // go back to cw generator menu
      if (buttonPressed != NONE) {
        buttonPressed = NONE;
        taskStep = 0;
      }
      break;
  }
}


void process_SETTINGS() {
  switch (taskStep) {
    case 0: 
      if (systemWasState != systemState) {
        systemWasState = systemState;
        Serial.println("SETTINGS");
      }
      blankLine(0);
      displayLine(1, "Settings", 0);
      displayLine(2, "Change any setting:", 0);
      processPref(true);  // process prefs first time
      taskStep = 11;
      break;
    case 11:  // process Settings
      if (buttonPressed != NONE) {
        processPref(false);
      }
      break;
    default:  // go back to choosing tool
      if (buttonPressed != NONE) {
        buttonPressed = NONE;
        systemState = START;
        taskStep = 1;
      }
      break;
  }
}


void process_HELP() {
  switch (taskStep) {
    case 0: 
      if (systemWasState != systemState) {
        systemWasState = systemState;
        Serial.println("HELP");
      }
      blankLine(0);
      displayLine(1, "Help", 0);
      scrollHighLight = 0;
      create_scroll_list(HelpTool, 20, 1);
      taskStep = 1;
      break;
    default:  // go back to choosing tool
      if (buttonPressed != NONE) {
        buttonPressed = NONE;
        systemState = START;
        taskStep = 1;
      }
      break;
  }
}


void process_SHUTDOWN() {  // going into deep sleep
      if (systemWasState != systemState) {
        systemWasState = systemState;
        Serial.println("SHUTDOWN");
      }
      blankLine(0);
      keyOut(true, notes[toneFreq], volume);  // turn on tone
      delay(dahLength);
      keyOut(false, notes[toneFreq], volume);  // turn off tone
      displayLine(1, "Shutdown", 0);
      displayLine(2, "Doing Deep Sleep.", 0);
      displayLine(3, "Bottom button wakes.", 0);
      displayLine(4, "Power off to", 0);
      displayLine(5, "save battery.", 0);
      delay(5000);
      esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0);
      esp_deep_sleep_start();
}


void process_ABOUT() {
  switch (taskStep) {
    case 0: 
      if (systemWasState != systemState) {
        systemWasState = systemState;
        Serial.println("ABOUT");
      }
      blankLine(0);
      displayLine(1, "About", 0);
      scrollHighLight = 0;
      create_scroll_list(toolAbout, 3, 1);
      taskStep++;
      break;
    default:  // go back to choosing tool
      if (buttonPressed != NONE) {
        buttonPressed = NONE;
        systemState = START;
        taskStep = 1;
      }
      break;
  }
}

//  end tasks


void loadPreferences() {

  Serial.println("Loading Preferences");

  pref.begin("PracticeMorse", false);  // false opens in read/write mode

  if (pref.isKey("showScrollHelp")) { showScrollHelp = pref.getBool("showScrollHelp"); }
  else { pref.putBool("showScrollHelp", showScrollHelp); }

  if (pref.isKey("didah")) { didah = pref.getBool("didah"); }
  else { pref.putBool("didah", didah); }

  if (pref.isKey("keyermode")) { keyermode = pref.getUChar("keyermode"); }
  else { pref.putUChar("keyermode", keyermode); }

  if (pref.isKey("interCharSpace")) { interCharSpace = pref.getUChar("interCharSpace"); }
  else { pref.putUChar("interCharSpace", interCharSpace); }

  if (pref.isKey("reversePolarity")) { reversePolarity = pref.getBool("reversePolarity"); }
  else { pref.putBool("reversePolarity", reversePolarity); }

  if (pref.isKey("ACSlength")) { ACSlength = pref.getUChar("ACSlength"); }
  else { pref.putUChar("ACSlength", ACSlength); }

  if (pref.isKey("toneFreq")) { toneFreq = pref.getUChar("toneFreq"); }
  else { pref.putUChar("toneFreq", (uint8_t) toneFreq); }

  if (pref.isKey("volume")) { volume = pref.getUChar("volume"); }
  else { pref.putUChar("volume", (uint8_t) volume); }

  if (pref.isKey("randomLength")) { randomLength = pref.getUChar("randomLength"); }
  else { pref.putUChar("randomLength", randomLength); }

  if (pref.isKey("randomOption")) { randomOption = pref.getUChar("randomOption"); }
  else { pref.putUChar("randomOption", randomOption); }

  if (pref.isKey("abbrevLength")) { abbrevLength = pref.getUChar("abbrevLength"); }
  else { pref.putUChar("abbrevLength", abbrevLength); }

  if (pref.isKey("wordLength")) { wordLength = pref.getUChar("wordLength"); }
  else { pref.putUChar("wordLength", wordLength); }

  if (pref.isKey("trainerDisplay")) { trainerDisplay = pref.getUChar("trainerDisplay"); }
  else { pref.putUChar("trainerDisplay", trainerDisplay); }

  if (pref.isKey("curtisBTiming")) { curtisBTiming = pref.getUChar("curtisBTiming"); }
  else { pref.putUChar("curtisBTiming", curtisBTiming); }

  if (pref.isKey("curtisBDotTimng")) { curtisBDotTimng = pref.getUChar("curtisBDotTimng"); }
  else { pref.putUChar("curtisBDotTimng", curtisBDotTimng); }

  if (pref.isKey("interWdSpace")) { interWdSpace = pref.getUChar("interWdSpace"); }
  else { pref.putUChar("interWdSpace", (uint8_t) interWdSpace); }

  if (pref.isKey("echoRepeats")) { echoRepeats = pref.getUChar("echoRepeats"); }
  else { pref.putUChar("echoRepeats", echoRepeats); }

  if (pref.isKey("echoDisplay")) { echoDisplay = pref.getUChar("echoDisplay"); }
  else { pref.putUChar("echoDisplay", echoDisplay); }

  if (pref.isKey("wordDoubler")) { wordDoubler = pref.getUChar("wordDoubler"); }
  else { pref.putUChar("wordDoubler", wordDoubler); }

  if (pref.isKey("echoToneShift")) { echoToneShift = pref.getUChar("echoToneShift"); }
  else { pref.putUChar("echoToneShift", echoToneShift); }

  if (pref.isKey("echoConf")) { echoConf = pref.getBool("echoConf"); }
  else { pref.putBool("echoConf", echoConf); }

  if (pref.isKey("speedAdapt")) { speedAdapt = pref.getBool("speedAdapt"); }
  else { pref.putBool("speedAdapt", speedAdapt); }

  if (pref.isKey("latency")) { latency = pref.getUChar("latency"); }
  else { pref.putUChar("latency", latency); }

  if (pref.isKey("randomFile")) { randomFile = pref.getUChar("randomFile"); }
  else { pref.putUChar("randomFile", randomFile); }

  if (pref.isKey("autoStopMode")) { autoStopMode = pref.getBool("autoStopMode"); }
  else { pref.putBool("autoStopMode", autoStopMode); }

  if (pref.isKey("maxSequence")) { maxSequence = pref.getUChar("maxSequence"); }
  else { pref.putUChar("maxSequence", maxSequence); }

  if (pref.isKey("responsePause")) { responsePause = pref.getUChar("responsePause"); }
  else { pref.putUChar("responsePause", responsePause); }

  if (pref.isKey("wpm")) { wpm = pref.getUChar("wpm"); }
  else { pref.putUChar("wpm", wpm); }

  if (pref.isKey("promptPause")) { promptPause = pref.getUChar("promptPause"); }
  else { pref.putUChar("promptPause", promptPause); }

  if (pref.isKey("wordPause")) { wordPause = pref.getUChar("wordPause"); }
  else { pref.putUChar("wordPause", wordPause); }

  if (pref.isKey("fileWdPointer1")) { fileWdPointer1 = pref.getULong("fileWdPointer1"); }
  else { pref.putULong("fileWdPointer1", fileWdPointer1); }

  if (pref.isKey("fileWdPointer2")) { fileWdPointer2 = pref.getULong("fileWdPointer2"); }
  else { pref.putULong("fileWdPointer2", fileWdPointer2); }

  if (pref.isKey("fileWdPointer3")) { fileWdPointer3 = pref.getULong("fileWdPointer3"); }
  else { pref.putULong("fileWdPointer3", fileWdPointer3); }

  if (pref.isKey("fileWdPointer4")) { fileWdPointer4 = pref.getULong("fileWdPointer4"); }
  else { pref.putULong("fileWdPointer4", fileWdPointer4); }

  if (pref.isKey("saveGenMode")) { saveGenMode = pref.getUChar("saveGenMode"); }
  else { pref.putUChar("saveGenMode", (uint8_t) saveGenMode); }

  if (pref.isKey("kochOrder")) { kochOrder = pref.getUChar("kochOrder"); }
  else { pref.putUChar("kochOrder", kochOrder); }

  if (pref.isKey("kochLesson")) { kochLesson = pref.getUChar("kochLesson"); }
  else { pref.putUChar("kochLesson", kochLesson); }

  if (pref.isKey("lcwoLesson")) { lcwoLesson = pref.getUChar("lcwoLesson"); }
  else { pref.putUChar("lcwoLesson", lcwoLesson); }

  if (pref.isKey("cwacLesson")) { cwacLesson = pref.getUChar("cwacLesson"); }
  else { pref.putUChar("cwacLesson", cwacLesson); }

  if (pref.isKey("licwLesson")) { licwLesson = pref.getUChar("licwLesson"); }
  else { pref.putUChar("licwLesson", licwLesson); }

  if (pref.isKey("kochSingle")) { kochSingle = pref.getUChar("kochSingle"); }
  else { pref.putUChar("kochSingle", kochSingle); }

  if (pref.isKey("kochShowMorse")) { kochShowMorse = pref.getBool("kochShowMorse"); }
  else { pref.putBool("kochShowMorse", kochShowMorse); }

  if (pref.isKey("kochCWTimer")) { kochCWTimer = pref.getUChar("kochCWTimer"); }
  else { pref.putUChar("kochCWTimer", kochCWTimer); }

  pref.end();
}


void savePreferences() {

  Serial.println("Saving Preferences");

  pref.begin("PracticeMorse", false);  // false opens in read/write mode

  if (showScrollHelp != pref.getBool("showScrollHelp")) {
    pref.putBool("showScrollHelp", showScrollHelp); }

  if (didah != pref.getBool("didah")) {
    pref.putBool("didah", didah); }

  if (keyermode != pref.getUChar("keyermode")) {
    pref.putUChar("keyermode", keyermode); }

  if (interCharSpace != pref.getUChar("interCharSpace")) {
    pref.putUChar("interCharSpace", interCharSpace); }

  if (reversePolarity != pref.getBool("reversePolarity")) {
    pref.putBool("reversePolarity", reversePolarity); }

  if (ACSlength != pref.getUChar("ACSlength")) {
    pref.putUChar("ACSlength", ACSlength); }

  if (toneFreq != (uint8_t) pref.getUChar("toneFreq")) {
    pref.putUChar("toneFreq", (uint8_t) toneFreq); }

  if (volume != (uint8_t) pref.getUChar("volume")) {
    pref.putUChar("volume", (uint8_t) volume); }

  if (randomLength != pref.getUChar("randomLength")) {
    pref.putUChar("randomLength", randomLength); }

  if (randomOption != pref.getUChar("randomOption")) {
    pref.putUChar("randomOption", randomOption); }

  if (abbrevLength != pref.getUChar("abbrevLength")) {
    pref.putUChar("abbrevLength", abbrevLength); }

  if (wordLength != pref.getUChar("wordLength")) {
    pref.putUChar("wordLength", wordLength); }

  if (trainerDisplay != pref.getUChar("trainerDisplay")) {
    pref.putUChar("trainerDisplay", trainerDisplay); }

  if (curtisBTiming != pref.getUChar("curtisBTiming")) {
    pref.putUChar("curtisBTiming", curtisBTiming); }

  if (curtisBDotTimng != pref.getUChar("curtisBDotTimng")) {
    pref.putUChar("curtisBDotTimng", curtisBDotTimng); }

  if (interWdSpace != (uint8_t) pref.getUChar("interWdSpace")) {
    pref.putUChar("interWdSpace", (uint8_t) interWdSpace); }

  if (echoRepeats != pref.getUChar("echoRepeats")) {
    pref.putUChar("echoRepeats", echoRepeats); }

  if (echoDisplay != pref.getUChar("echoDisplay")) {
    pref.putUChar("echoDisplay", echoDisplay); }

  if (wordDoubler != pref.getUChar("wordDoubler")) {
    pref.putUChar("wordDoubler", wordDoubler); }

  if (echoToneShift != pref.getUChar("echoToneShift")) {
    pref.putUChar("echoToneShift", echoToneShift); }

  if (echoConf != pref.getBool("echoConf")) {
    pref.putBool("echoConf", echoConf); }

  if (speedAdapt != pref.getBool("speedAdapt")) {
    pref.putBool("speedAdapt", speedAdapt); }

  if (latency != pref.getUChar("latency")) {
    pref.putUChar("latency", latency); }

  if (randomFile != pref.getUChar("randomFile")) {
    pref.putUChar("randomFile", randomFile); }

  if (autoStopMode != pref.getUChar("autoStopMode")) {
    pref.putUChar("autoStopMode", autoStopMode); }

  if (maxSequence != pref.getUChar("maxSequence")) {
    pref.putUChar("maxSequence", maxSequence); }

  if (responsePause != pref.getUChar("responsePause")) {
    pref.putUChar("responsePause", responsePause); }

  if (wpm != pref.getUChar("wpm")) {
    pref.putUChar("wpm", wpm); }

  if (promptPause != pref.getUChar("promptPause")) {
    pref.putUChar("promptPause", promptPause); }

  if (wordPause != pref.getUChar("wordPause")) {
    pref.putUChar("wordPause", wordPause); }

  if (saveGenMode != pref.getUChar("saveGenMode")) {
    pref.putUChar("saveGenMode", (uint8_t) saveGenMode); }

  if (kochOrder != pref.getUChar("kochOrder")) {
    pref.putUChar("kochOrder", kochOrder); }

  if (kochLesson != pref.getUChar("kochLesson")) {
    pref.putUChar("kochLesson", kochLesson); }

  if (lcwoLesson != pref.getUChar("lcwoLesson")) {
    pref.putUChar("lcwoLesson", lcwoLesson); }

  if (cwacLesson != pref.getUChar("cwacLesson")) {
    pref.putUChar("cwacLesson", cwacLesson); }

  if (licwLesson != pref.getUChar("licwLesson")) {
    pref.putUChar("licwLesson", licwLesson); }

  if (kochSingle != pref.getUChar("kochSingle")) {
    pref.putUChar("kochSingle", kochSingle); }

  if (kochShowMorse != pref.getBool("kochShowMorse")) {
    pref.putBool("kochShowMorse", kochShowMorse); }

  if (kochCWTimer != pref.getUChar("kochCWTimer")) {
    pref.putUChar("kochCWTimer", kochCWTimer); }

  pref.end();
}


void saveWordPointers() {

  Serial.println("Saving Word Pointers");

  pref.begin("PracticeMorse", false);  // false opens in read/write mode

  if (fileWdPointer1 != pref.getULong("fileWdPointer1")) {
    pref.putULong("fileWdPointer1", fileWdPointer1); }

  if (fileWdPointer2 != pref.getULong("fileWdPointer2")) {
    pref.putULong("fileWdPointer2", fileWdPointer2); }

  if (fileWdPointer3 != pref.getULong("fileWdPointer3")) {
    pref.putULong("fileWdPointer3", fileWdPointer3); }

  if (fileWdPointer4 != pref.getULong("fileWdPointer4")) {
    pref.putULong("fileWdPointer4", fileWdPointer4); }

  pref.end();
}


/*********************************************************/

// keyer stuff

// check paddles, return true if a paddle is active 
// sets globals leftKey rightKey
boolean checkPaddles() {

  // static use here preserves the value over multiple multiple calls to function
  static boolean oldL = false, newL, oldR = false, newR;
  int left, right;
  static long lTimer = 0, rTimer = 0;
  const int debDelay = 512;       // debounce time = 0.512  ms
  
  // change pin polarity if needed
  left = reversePolarity ? rightPin : leftPin;
  right = reversePolarity ? leftPin : rightPin;
                                                          // read external paddle presses
  newL = !digitalRead(left);                    // tip/dot (=left) always, to be able to use straight key to initiate echo trainer etc
  if (keyermode != STRAIGHTKEY) {               
      newR = !digitalRead(right);               // ring/dash (=right) only when in straight key mode, to prevent continuous activation 
  }                                                       // when used with a 2-pole jack on the straight key
  
  if ((keyermode == NONSQUEEZE) && newL && newR)  // both paddles pressed in 
    return (leftKey || rightKey);  // NONSQUEEZE mode, just return previous value
  
  if (newL != oldL)
      lTimer = micros();
  if (newR != oldR)
      rTimer = micros();
  if (micros() - lTimer > debDelay)
      if (newL != leftKey) 
          leftKey = newL;
  if (micros() - rTimer > debDelay)
      if (newR != rightKey) 
          rightKey = newR;       

  oldL = newL;
  oldR = newR;
  return (leftKey || rightKey);
}


// use the paddles for iambic keying
boolean doPaddleIambic (boolean dit, boolean dah) {
  boolean paddleSwap;        // temp storage if we need to swap left and right
  static long ktimer;        // timer for current element (dit or dah)
  static long curtistimer;   // timer for early paddle latch in Curtis mode B+
  static long latencytimer;  // timer for "muting" paddles for some time in state INTER_ELEMENT
  unsigned int pitch;

//  if(keyermode == STRAIGHTKEY) {
//    keyDecoder.decode();
//    updateManualSpeed();
//    return false;
//  }
  if (!didah)   {  // swap left and right values if necessary!
      paddleSwap = dit; dit = dah; dah = paddleSwap; 
  }
      
  switch (keyerState) {  // this is the keyer state machine
    case IDLE_STATE:
      // display the interword space, if necessary
      if (millis() > interWordTimer) {
        // print a space on the display
        Serial.print(" ");
        tft.print(" ");
        interWordTimer = 4294967000;  // almost the biggest possible unsigned long number :-) - do not output extra spaces!
        if (echoTrainerState == COMPLETE_ANSWER) {  // change the state of the trainer at end of word
          echoTrainerState = EVAL_ANSWER;
          return false;
        }
      }
        
      // Was there a paddle press?
      if (dit || dah ) {
        updatePaddleLatch(dit, dah);  // trigger the paddle latches
        if (systemState == ECHOTRAINER || systemState == KOCHTRAINER) {  // change the state of the trainer at end of word
          echoTrainerState = COMPLETE_ANSWER;
        }
        C = 0;  // start building new character from keyer input
        //keyerTable.resetTable();
        if (dit) {
          setDITstate();  // set next state
          DIT_FIRST = true;  // first paddle pressed after IDLE was a DIT
        }
        else {
          setDAHstate();  
          DIT_FIRST = false;  // first paddle was a DAH
        }
      }
      else {
        if (echoTrainerState == GET_ANSWER && millis() > genTimer) {
        echoTrainerState = EVAL_ANSWER;
        } 
        return false;  // we return false if there was no paddle press in IDLE STATE - Arduino can do other tasks for a bit
      }
      break;

    case DIT:
      // first we check that we have waited as defined by ACS settings
      if ( ACSlength > 0 && (millis() <= acsTimer))  // if we do automatic character spacing, and haven't waited for (3 or whatever) dits...
        break;
      clearPaddleLatches();  // always clear the paddle latches at beginning of new element
      keyerControl |= DIT_LAST;  // remember that we process a DIT

      ktimer = ditLength;  // prime timer for dit
      switch ( keyermode ) {
        case IAMBICB:  
          curtistimer = 2 + (ditLength * curtisBDotTimng / 100);   
          break;  // enhanced Curtis mode B starts checking after some time
        case NONSQUEEZE:
          curtistimer = 3;
          break;
        default:
          curtistimer = ditLength;  // no early paddle checking in Curtis mode A Ultimatic mode or Non-squeeze
          break;
        }
        keyerState = KEY_START;  // set next state of state machine
        break;
            
    case DAH:
      if ( ACSlength > 0 && (millis() <= acsTimer))  // if we do automatic character spacing, and haven't waited for 3 dits...
        break;
      clearPaddleLatches();  // clear the paddle latches
      keyerControl &= ~(DIT_LAST);  // clear dit latch  - we are not processing a DIT
            
      ktimer = dahLength;
      switch (keyermode) {
        case IAMBICB:  curtistimer = 2 + (dahLength * curtisBTiming / 100);    // enhanced Curtis mode B starts checking after some time
          break;
        case NONSQUEEZE:
          curtistimer = 3;
          break;
        default:
          curtistimer = dahLength;  // no early paddle checking in Curtis mode A or Ultimatic mode
          break;
      }
      keyerState = KEY_START;  // set next state of state machine
      break;
     
    case KEY_START:
      // Assert key down, start timing, state shared for dit or dah
      //**// get current pitch tone
      pitch = notes[toneFreq];
      if ((systemState == ECHOTRAINER || systemState == KOCHTRAINER) && echoToneShift != 0) {
        pitch = (echoToneShift == 1 ? pitch * 18 / 17 : pitch * 17 / 18);        /// one half tone higher or lower, as set in parameters in echo trainer mode
      }

      keyOut(true, pitch, volume);  // turn on tone
      ktimer += millis();  // set ktimer to interval end time          
      curtistimer += millis();  // set curtistimer to curtis end time
      keyerState = KEYED;  // next state
      break;
 
    case KEYED:
      // Wait for timers to expire
      if (millis() >= ktimer) {  // are we at end of key down ?
        keyOut(false, notes[toneFreq], volume);  // turn off tone
        ktimer = millis() + ditLength -1;  // inter-element time
        latencytimer = millis() + ((latency-1) * ditLength / 8);
        keyerState = INTER_ELEMENT;  // next state
      }
      else if (millis() >= curtistimer ) {  // in Curtis mode we check paddle as soon as Curtis time is off
        if (keyerControl & DIT_LAST)  // last element was a dit
          updatePaddleLatch(false, dah);  // not sure here: we only check the opposite paddle - should be ok for Curtis B
        else
          updatePaddleLatch(dit, false);  
          // updatePaddleLatch(dit, dah);  // but we remain in the same state until element time is off! 
      }
      break;
 
    case INTER_ELEMENT:
      //if ((p_keyermode != NONSQUEEZE) && (millis() < latencytimer)) {  // or should it be p_keyermode > 2 ? Latency for Ultimatic mode?
      if (millis() < latencytimer) {
        if (keyerControl & DIT_LAST)  // last element was a dit
          updatePaddleLatch(false, dah);  // not sure here: we only check the opposite paddle - should be ok for Curtis B
        else
          updatePaddleLatch(dit, false);
      }
      else {
        updatePaddleLatch(dit, dah);  // latch paddle state while between elements       
        if (millis() >= ktimer) {  // at end of INTER-ELEMENT
          switch(keyerControl) {
            // both paddles are latched
            case 3:  
            case 7: 
              switch (keyermode) {
                case STRAIGHTKEY: 
                  break;
                case NONSQUEEZE:  
                  if (DIT_FIRST)  // when first element was a DIT
                    setDITstate();  // next element is a DIT again
                  else  // but when first element was a DAH
                    setDAHstate();  // the next element is a DAH again! 
                  break;
                case ULTIMATIC: 
                  if (DIT_FIRST)  // when first element was a DIT
                    setDAHstate();  // next element is a DAH
                  else  // but when first element was a DAH
                    setDITstate();  // the next element is a DIT! 
                  break;
                default:
                  if (keyerControl & DIT_LAST)  // Iambic: last element was a dit - this is case 7, really
                    setDAHstate();  // next element will be a DAH
                  else  // and this is case 3 - last element was a DAH
                    setDITstate();  // the next element is a DIT       
                  break;                  
              }
              break;
            // dit only is latched, regardless what was last element  
            case 1:    
            case 5:  
              setDITstate();
              break;
            // dah only latched, regardless what was last element
            case 2:
            case 6:  
              setDAHstate();
              break;
            // none latched, regardless what was last element
            case 0:
            case 4:  
              keyerState = IDLE_STATE;  // we are at the end of the character and go back into IDLE STATE
              // display just finished character on the screen
              Serial.print(cleanUpProSigns(String(morse[C])));
              checkShiftUp(210);
              tft.print(cleanUpProSigns(String(morse[C])));  
              if ((systemState == ECHOTRAINER || systemState == KOCHTRAINER) 
                && echoTrainerState == COMPLETE_ANSWER 
                && echoResponse.length() < 30) {
                echoResponse += cleanUpProSigns(String(morse[C])); 
                if (echoResponse.length() > 11) {
                  echoTrainerState = EVAL_ANSWER;  // longer than 12 not allowed
                }
              }

//              ++charCounter;  // count this character
//              // if we have seen 12 chars since changing speed, we write the config to preferences (speed and left & right thresholds)
//              if (charCounter == 12) {
//                fireCharSeen(false);
//              }
              if (ACSlength > 0)
                acsTimer = millis() + ACSlength * ditLength; // prime the ACS timer
              //if (systemState == CWKEYER)
              //  interWordTimer = millis() + 5*ditLength;
              //else
                interWordTimer = millis() + interWordSpace;  // prime the timer to detect a space between characters
              // nominally 7 dit-lengths, but we are not quite so strict here in keyer mode,
              // use the extended time in echo trainer mode to allow longer space between characters, 
              // like in listening
              keyerControl = 0;  // clear all latches completely before we go to IDLE
              break;
          } // switch keyerControl : evaluation of flags
        }
      } // end of INTER_ELEMENT
      break;
  } // end switch keyerState - end of state machine

  if (keyerControl & 3)  // any paddle latch?                            
    return true;  // we return true - we processed a paddle press
  else
    return false;  // when paddle latches are cleared, we return false
} // end function doPaddleIambic()


// keyer subroutines

// update the paddle latches in keyerControl
void updatePaddleLatch(boolean dit, boolean dah)
{ 
  if (dit)
    keyerControl |= DIT_L;
  if (dah)
    keyerControl |= DAH_L;
}

 
// clear the paddle latches in keyer control
void clearPaddleLatches ()
{
  keyerControl &= ~(DIT_L + DAH_L);  // clear both paddle latch bits
}


// functions to set DIT and DAH keyer states
void setDITstate() {
  keyerState = DIT;
  if (C == 0) { C = 1; }
  Serial.print(".");
  if (C < 128) { C <<= 1; }
   //keyerTable.recordDit();
//  if (morseState == loraTrx || morseState == wifiTrx)
//    cwForTx(1);  // build compressed string for LoRa & Wifi
}


void setDAHstate() {
  keyerState = DAH;
  if (C == 0) { C = 1; }
  Serial.print("-");
  if (C < 128) { C = (C << 1) | 1; }
  //keyerTable.recordDah();
//  if (morseState == loraTrx || morseState == wifiTrx)
//    cwForTx(2);
}


// toggle polarity of paddles
void togglePolarity () {
      didah = !didah; 
}


void updateTimings() {
  ditLength = 1200 / wpm;  // set new value for length of dits and dahs and other timings
  dahLength = 3 * ditLength;
  interCharacterSpace =  interCharSpace * ditLength;
  halfICS = (3 + interCharSpace) * ditLength / 2;
  interWordSpace = max(interWdSpace, interCharSpace+4) * ditLength;
  effWpm = 60000 / (31 * ditLength + 4 * interCharacterSpace + interWordSpace );  ///  effective wpm with lengthened spaces = Farnsworth speed
} 


// turns on and off a tone with frequency f
void keyOut(boolean on, int freq, int vol) {                                      
  // generate a tone with frequency f when on==true, or turn it off

  int duty = vol * 12;
  if (volume == 10) { duty = 127; }

  ledcSetup(TONE_PWM_CHANNEL, freq, LEDC_TIMER_12_BIT);  // set frequency

  if (on) {  // turn tone on
    ledcWrite(TONE_PWM_CHANNEL, duty);
    //ledcWriteTone(TONE_PWM_CHANNEL, freq);
  } else {  // turn tone off
    ledcWrite(TONE_PWM_CHANNEL, 0);
    //ledcWriteTone(TONE_PWM_CHANNEL, 0);
  }
}


void checkStopFlag() {
    if (stopFlag) {
      active = stopFlag = false;
      keyOut(false, notes[toneFreq], volume);
//      if (systemState == ECHOTRAINER) {
//        MorseOutput::clearStatusLine();
//        MorseOutput::printOnStatusLine( true, 0, String(errCounter) + " errs (" + String(wordCounter-2) + " wds)" );
//        delay(5000);
//      }
      wordCounter = 1; errCounter = 0;
//      MorseOutput::printOnStatusLine( true, 0, "Continue w/ Paddle");
    }
}


void startNotice(int line) {
  displayLine(line, "WPM set to " + String(wpm), 0);
  delay(1000);
  displayLine(line+1, "3", 0);
  keyOut(true, notes[0], volume);  // turn on tone
  delay(dahLength);
  keyOut(false, notes[toneFreq], volume);  // turn off tone
  delay(1000 - dahLength);
  displayLine(line+1, "2", 0);
  keyOut(true, notes[0], volume);  // turn on tone
  delay(dahLength);
  keyOut(false, notes[0], volume);  // turn off tone
  delay(1000 - dahLength);
  displayLine(line+1, "1", 0);
  keyOut(true, notes[0], volume);  // turn on tone
  delay(dahLength);
  keyOut(false, notes[0], volume);  // turn off tone
  delay(1000 - dahLength);
  displayLine(line+1, "GO", 0);
  keyOut(true, notes[7], volume);  // turn on tone
  delay(dahLength);
  keyOut(false, notes[7], volume);  // turn off tone
  delay(1000 - dahLength);
  blankLine(line+1);
  blankLine(line);
}


void cleanStartSettings() {
    clearText = "";
    CWword = "";
    echoTrainerState = START_ECHO;
    generatorState = KEY_UP; 
    keyerState = IDLE_STATE;
    interWordTimer = 4294967000;                 // almost the biggest possible unsigned long number :-) - do not output a space at the beginning
    genTimer = millis()-1;                       // we will be at end of KEY_DOWN when called the first time, so we can fetch a new word etc... 
    errCounter = wordCounter = 0;                // reset word and error counter for maxSequence
    startFirst = true;
    autoStop = nextword;                         // for autoStop mode
    kochCWTime = millis();
}


void generateCW() {  // called frequently from loop() and generates CW
  
  static int l;
  static char c;
  boolean silentEcho;
  
  switch (generatorState) {  // CW generator state machine 
    // pseudo key is up or down
    case KEY_UP:
      if (millis() < genTimer)  // not yet at end of the pause: just wait
          return;               // therefore return to loop()
      // continue if the pause has been long enough

      l = CWword.length();
      
      if (l==0)  {  // fetch a new word if we have an empty word
        if (clearText.length() > 0) {  // this should not be reached  
                                       // except when display word by word
          if (trainerDisplay == DISPLAY_BY_WORD && 
              (systemState == CWGENERATOR || systemState == KOCHTRAINER)) {
            displayGeneratedMorse(cleanUpProSigns(clearText));
            clearText = "";
          }
        }
        fetchNewWord();
        if (CWword.length() == 0)  // should have something here unless in a pause
          return;                  // in this case return
        if (systemState == ECHOTRAINER || systemState == KOCHTRAINER) {
          if (startFirst) {
            startFirst = false;
          } else {  // possible new line after each try
            if (generatorMode == KOCH_LEARN && startItemSelected == 2
              && (millis() - kochCWTime) < (kochCWTimer*10*1000)) { // no new line
              checkShiftUp(210);
            } else {  // new line after each try
              if (tft.getCursorY() < 109) {
                  tft.print("\n");
              } else { checkShiftUp(1); } 
            }
          }
        }
      }
      c = CWword[0];  // get next element from CWword; if 0, we were at end of character
      CWword.remove(0,1); 
      if (c == '0' || !CWword.length())  {  // a character just had been finished
        if (c == '0') {
          c = CWword[0];  // retrieve next element from CWword;
          CWword.remove(0,1); 
        }  
      }   /// at end of character

      //// insert code here for outputting only on display, 
      //// and not as morse characters - for echo trainer
      //// genTimer vy short (1ms?)
      //// no keyOut()
      if ((systemState == ECHOTRAINER || systemState == KOCHTRAINER) && echoDisplay == DISP_ONLY)
          genTimer = millis() + 2;      // very short timing
      else 
          genTimer = millis() + (c == '1' ? ditLength : dahLength);  // start a dit or a dah, acording to the next element
      /// if Koch learn character we show dit or dah
      if (systemState == KOCHTRAINER && kochShowMorse)
          displayGeneratedMorse(c == '1' ? "." : "-");
      silentEcho = ((systemState == ECHOTRAINER || systemState == KOCHTRAINER) && echoDisplay == DISP_ONLY); 
        // echo mode with no audible prompt
      if (silentEcho || stopFlag)  // finished maxSequence so start output 
        ;
      else {
          keyOut(true, pitch(), volume);
      }
      generatorState = KEY_DOWN;  // next state = key down = dit or dah
      break;

    case KEY_DOWN:
      if (millis() < genTimer)  // if not at end of key down need to wait
          return;               // so just return to loop()
      // otherwise continue here; stop keying, and determine the length of the 
      // following pause: interElement, interCharacter or InterWord?
      keyOut(false, notes[toneFreq], volume);
      if (!CWword.length())  {  // we just ended the the word 
        //// intercept here in Echo Trainer mode or autoStop mode
        if (systemState == CWGENERATOR) {
          autoStop = autoStopMode ? halt : nextword;
        }
        dispGeneratedChar();
        if (systemState == ECHOTRAINER || (systemState == KOCHTRAINER 
          && startItemSelected == 3)) {  // echo trainer in koch
          switch (echoTrainerState) {
            case START_ECHO:  
              echoTrainerState = SEND_WORD;
              genTimer = millis() + interCharacterSpace + (promptPause * interWordSpace);
              break;
            case REPEAT_WORD:
              // fall through 
            case SEND_WORD: 
              if (echoStop)
                break;
              else {
                if (generatorMode == KOCH_LEARN && startItemSelected == 2 
                  && (millis() - kochCWTime) < (kochCWTimer*10*1000)) {
                  // in Koch CW Generator
                  genTimer = millis() + interWordSpace;
                } else {
                  echoTrainerState = GET_ANSWER;
                  if (echoDisplay != CODE_ONLY){
                    displayGeneratedMorse(" > ");  // prompt between echo and response
                  }
                  ++repeats;
                  genTimer = millis() + responsePause * interWordSpace;
                  kochCWTime = genTimer;
                }
              }
              break;
            default:         
              break;
          }
        } else if (generatorMode == KOCH_LEARN && startItemSelected == 2) {
          // in Koch CW Generator
          if ((millis() - kochCWTime) < (kochCWTimer*10*1000)) {  // wait for kochCWTimer
            genTimer = millis() + interWordSpace;
          } else {  // kochCWTimer expired
            if (echoDisplay != CODE_ONLY){
              displayGeneratedMorse(" > ");  // prompt after a group of strings
              active = !active;  // pause after a group of strings
              Serial.println(" ");
            }
            genTimer = millis() + responsePause * interWordSpace;
            kochCWTime = genTimer;
          }
        }
        else { 
          //genTimer = millis() + interWordSpace;  // we need a pause for interWordSpace
          genTimer = millis() + interWordSpace + (wordPause * 1000);  // we need a pause for interWordSpace
        }
      } else if ((c = CWword[0]) == '0') {                                                                        // we are at end of character
//        // display last character 
//        // genTimer small if in echo mode and no code!
        dispGeneratedChar(); 
        if ((systemState == ECHOTRAINER || systemState == KOCHTRAINER) && echoDisplay == DISP_ONLY)
          genTimer = millis() +1;
        else            
          genTimer = millis() + wordDoublerICS();  // pause = intercharacter space
      } else {  // we are in the middle of a character
        genTimer = millis() + ditLength;  // pause = interelement space
      }
      generatorState = KEY_UP;  // next state = key up = pause
      break;         
  }   // end switch (generatorState)
}


int pitch() {  // find out which pitch to use for the generated CW tone
    int p = notes[toneFreq];
    if (alternatePitch && systemState == CWGENERATOR)
      p = (echoToneShift == 1 ? p * 18 / 17 : p * 17 / 18);
    return p;
}


void dispGeneratedChar() {
  static String charString;
  charString.reserve(20);
  
  if (generatorMode == KOCH_LEARN ||
    (trainerDisplay == DISPLAY_BY_CHAR && systemState == CWGENERATOR) ||
    (systemState == ECHOTRAINER && echoDisplay != CODE_ONLY ))  {
    // we need to output the character on the display now  
    if (clearText.charAt(0) == 0xC3) {  //UTF-8!
      charString = String(clearText.charAt(0)) + String(clearText.charAt(1));  // store first 2 chars of clearText in charString
      clearText.remove(0,2);  // and remove them from clearText
    } else {
      charString = clearText.charAt(0);
      clearText.remove(0,1);
    }
//    if (generatorMode == KOCH_LEARN)
//      displayGeneratedMorse(" ");  // output a space
    displayGeneratedMorse(cleanUpProSigns(charString));
//    if (generatorMode == KOCH_LEARN)
//      displayGeneratedMorse(" ");  // output a space
  }   //// end display_by_char
      
//  ++charCounter;  // count this character
     
  // if we have seen 12 chars since changing speed, we write the config to Preferences
//  if (charCounter == 12) {
//    MorsePreferences::fireCharSeen(true);
//  }
}


void displayGeneratedMorse(String s) {
  checkShiftUp(210);
  for (int i = 0; i < s.length(); i++) {  // one char at a time 
    tft.print(s.charAt(i));               // to give wrap a chance
    delay(15);
  }
}


String cleanUpProSigns( String input ) {
  String output = "";
  /// clean up clearText   -   s <as> - a <ka> - n <kn> - k <sk> - etc
  for (int i = 0; i < input.length(); i++) {
    switch (input.charAt(i)) {
      case 's': output += "<as>"; break;
      case 'a': output += "<ka>"; break;
      case 'n': output += "<kn>"; break;
      case 'k': output += "<sk>"; break;
      case 'e': output += "<ve>"; break;
      case 'b': output += "<bk>"; break;
      case 'h': output += "ch"; break;
      case 'u': output += "*"; break;
      default: output += input.charAt(i); break;
    }
  }
  return output;
}


void fetchNewWord() {  // get new word from multiple sources
  // when done:
  //  clearText will have text of word
  //  CWword will have code representation of word
  
  int rv;
  char numBuffer[16];                // for number to string conversion with sprintf()

  //if ((morseState == morseGenerator) && !MorsePreferences::autoStopMode ) {
  if ( systemState == CWGENERATOR || systemState == KOCHTRAINER ) {
    displayGeneratedMorse(" ");  // add a blank after the word on the display
  }
  
  if (generatorMode == KOCH_LEARN) {
      //startFirst = false;
      echoTrainerState = SEND_WORD;
  }
///  if (startFirst == true)  {  // do the intial sequence in trainer mode, too
///    clearText = "";
///    startFirst = false;
///  } else if (systemState == CWGENERATOR && wordDoubler != 0 && firstTime == false) {
  if (systemState == CWGENERATOR && wordDoubler != 0 && firstTime == false) {
    clearText = echoTrainerWord;
    firstTime = true;
  } else if (systemState == CWGENERATOR && autoStop == repeatword) {
    clearText = echoTrainerWord;
    autoStop = nextword;
  } else if (systemState == ECHOTRAINER || systemState == KOCHTRAINER) {
    interWordTimer = 4294967000;  // interword timer should not trigger now
    switch (echoTrainerState) {
      case  REPEAT_WORD:  
        if (echoRepeats == 7 || repeats <= echoRepeats) 
          clearText = echoTrainerWord;
        else {
          clearText = echoTrainerWord;
          if (generatorMode != KOCH_LEARN) {
            displayGeneratedMorse(" ");
            displayGeneratedMorse(cleanUpProSigns(clearText));  // clean up first!
          }
          goto randomGenerate;
        }
        break;
      case  START_ECHO:
      case  SEND_WORD: 
        goto randomGenerate;
        break;
      default: 
        break;
    }   /// end special cases for echo Trainer
  } else {   
 
  randomGenerate:       
    repeats = 0;
    clearText = "";
    //if ((maxSequence != 0) && (generatorMode != KOCH_LEARN))
    if (maxSequence != 0 && generatorMode != KOCH_LEARN) {
      if (systemState == ECHOTRAINER || systemState == KOCHTRAINER || ((systemState == CWGENERATOR) && !autoStopMode)) {
        // a case for maxSequence - no maxSequence in autostop mode                         
        wordCounter++;  // 
        int limit = 1 + maxSequence;
        if (wordCounter == limit) {
          clearText = "+";
          echoStop = true;
          if (echoTrainerState == REPEAT_WORD)
            echoTrainerState = SEND_WORD;
        } else if (wordCounter == (limit+1)) {
          stopFlag = true;
          echoStop = false;
          //wordCounter = 1;
        }
      }
    }
    if (clearText != "+") {
      switch (generatorMode) {
        case RANDOMS:  // random chars
          clearText = getRandomChars(randomLength, randomOption);
          break;
/*
        case CALLS:    
          clearText = getRandomCall(callLength);
          break;
*/
        case ABBREVS:  // abbreviations
          clearText = getRandomAbbrev(abbrevLength);
          clearText.toUpperCase();
          break;

        case WORDS:  // words 
          clearText = getRandomWord(wordLength);
          clearText.toUpperCase();
          break;

        case KOCH_LEARN:
          clearText = kochGetNewChar();
          break;
          
        case MIXED:  // mixed chars, abbrevs, words  
          rv = random(3);
          switch (rv) {
            case  0:  
              clearText = getRandomWord(wordLength);
              clearText.toUpperCase();
              break;
            case  1:  
              clearText = getRandomAbbrev(abbrevLength);
              clearText.toUpperCase();
              break;
            case  2:  
              clearText = getRandomChars(1,OPT_PUNCTPRO);        // just a single pro-sign or interpunct
              break;
/*              
            case  3:  
              clearText = getRandomCall(callLength);
              break;
*/
          }
          break;
/*  
        case KOCH_MIXED:
          rv = random(3);
          switch (rv) {
            case  0:  
              clearText = getRandomWord(wordLength);
              break;
            case  1:  
              clearText = getRandomAbbrev(abbrevLength);
              break;
            case  2:  
              clearText = getRandomChars(randomLength, OPT_KOCH);        // Koch option!
              break;
          }
          break;
        case KOCH_ADAPTIVE:
          clearText = getRandomChars(randomLength, OPT_KOCH_ADAPTIVE);
          break;
*/
        case WORDS2:  // 2 word phrases  
          openCorrectFile(2); 
          if (startWords2) {  
            if (randomFile || fileWordPointer != 0) {  
              if (randomFile) {  // get random phrase
                skipWords(random(randomFile+1));
              }
              clearText = getWord(); 
              while ( clearText != "=" ) {  // move to start of next phrase
                clearText = getWord(); 
              }
            }
          }
          startWords2 = false;
          // get next word
          clearText = getWord(); 
          if ( clearText == "=" || clearText == "?" ) {  // don't print = or ?
            tft.print(" ");
            startWords2 = true;
            if ( wordPause == 0 ) {
              delay(1000);
            }
            clearText = ""; 
          }
          if (clearText != "t") { clearText.toUpperCase(); }
          fileWdPointer2 = fileWordPointer;
          break;  

        case WORDS3:  // 3 word phrases  
          openCorrectFile(3); 
          if (startWords3) {  
            if (randomFile || fileWordPointer != 0) {  
              if (randomFile) {  // get random phrase
                skipWords(random(randomFile+1));
              }
              clearText = getWord(); 
              while ( clearText != "=" ) {  // move to start of next phrase
                clearText = getWord(); 
              }
            }
          }
          startWords3 = false;
          // get next word
          clearText = getWord(); 
          if ( clearText == "=" || clearText == "?" ) {  // don't print = or ?
            tft.print(" ");
            startWords3 = true;
            if ( wordPause == 0 ) {
              delay(1000);
            }
            clearText = ""; 
          }
          if (clearText != "t") { clearText.toUpperCase(); }
          fileWdPointer3 = fileWordPointer;
          break;  

        case WORDS4:  // 4 word phrases  
          openCorrectFile(4); 
          if (startWords4) {  
            if (randomFile || fileWordPointer != 0) {  
              if (randomFile) {  // get random phrase
                skipWords(random(randomFile+1));
              }
              clearText = getWord(); 
              while ( clearText != "=" ) {  // move to start of next phrase
                clearText = getWord(); 
              }
            }
          }
          startWords4 = false;
          // get next word
          clearText = getWord(); 
          if ( clearText == "=" || clearText == "?" ) {  // don't print = or ?
            tft.print(" ");
            startWords4 = true;
            if ( wordPause == 0 ) {
              delay(1000);
            }
            clearText = ""; 
          }
          if (clearText != "t") { clearText.toUpperCase(); }
          fileWdPointer4 = fileWordPointer;
          break;  

        case REALQSOS:  // real QSOs  
          openCorrectFile(1); 
          if (startWords1) {  
            if (randomFile || fileWordPointer != 0) {  
              if (randomFile) {  // get random QSO from QSO file
                skipWords(random(randomFile+1));
              }
              clearText = getWord(); 
              while ( clearText != "k" && clearText != "n" ) {  // move to start of next QSO
                clearText = getWord(); 
              }
            }
            tft.print(" ");
            if ( wordPause == 0 ) {
              delay(1000);
            }
          }
          startWords1 = false;
          // get next word
          clearText = getWord(); 
          if ( clearText == "=" || clearText == "?" ) {  // don't print = or ?
            tft.print(" ");
            if ( wordPause == 0 ) {
              delay(1000);
            }
            clearText = ""; 
          }
          if ( clearText == "k" || clearText == "n" ) {  // end of QSO
            startWords1 = true;
          }
          fileWdPointer1 = fileWordPointer;
          break;  

      }  // end switch (generatorMode)
    }
    firstTime = false;
  }  // end if else - either already had something in trainer mode, or got a new word
  //if (clearText.indexOf('p') != -1) {
  if (clearText == "p") {
    genTimer = millis() + (3 * interWordSpace);
    clearText = "";
    return;
  }
  //if (clearText.indexOf('t') != -1) {
  if (clearText == "t") {
    alternatePitch = !alternatePitch;
    clearText = "";
    return;
  }
  CWword = generateCWword(clearText);
  echoTrainerWord = clearText;
} // end of fetchNewWord()


String kochGetNewChar() {  // return lesson char or random group of chars
  String kochCharSet;
  uint8_t lesson;
  String newString = "";
  switch (kochOrder) {  // get correct char set
    case 0: kochCharSet = KochChars; lesson = kochLesson; break; 
    case 1: kochCharSet = LCWOChars; lesson = lcwoLesson; break; 
    case 2: kochCharSet = CWACChars; lesson = cwacLesson; break; 
    case 3: kochCharSet = LICWChars; lesson = licwLesson; break; 
  }
  if (kochSingle) {  // return lesson char
    return String(kochCharSet[lesson-1]);
  }
  for (int i = 0; i < random(1,10); i++) {  // generate a word with random chars
    newString = newString + String(kochCharSet[random(0,lesson)]);
  }
  return newString;
}

String generateCWword(String symbols) {  
  // return a code representation of CWchars
  int pointer;
  byte bitMask, NoE;
  //byte nextElement[8];  // the list of elements; 0 = dit, 1 = dah
  String result = "";
  
  int l = symbols.length();
  
  for (int i = 0; i<l; ++i) {
    char c = symbols.charAt(i);  // next char in string
//    pointer = CWchars.indexOf(c);  // at which position is the character in CWchars?
//    NoE = pool[pointer][1];  // how many elements in this morse code symbol?
//    bitMask = pool[pointer][0];  // bitMask indicates which of the elements are dots and which are dashes
    bitMask = morseA[c];
    boolean usebit = false;
    for (int j=0; j<8; ++j) {
      if (usebit) {
        result += (bitMask & B10000000 ? "2" : "1" );  // get MSB and store it in string - 2 is dah, 1 is dit, 0 = inter-element space
      }
      if (bitMask & B10000000) { usebit = true; }  // first 1 indicates start of morse
      bitMask = bitMask << 1;  // shift bitmask 1 bit to the left 
    }  // now we are at the end of one character, therefore we add enough space for inter-character
    result += "0";
    Serial.print(c);
  }  // now we are at the end of the word, therefore we remove the final 0!
  result.remove(result.length()-1);
  Serial.print(" ");
  return result;
}

  
  // use substrings as char pool for trainer mode
  // SANK will be replaced by <as>, <ka>, <kn> and <sk> (the 2nd letter is the key)
  // Options:
  //    0: a9?<> = CWchars; all of them; same as Koch 45+
  //    1: a = CWchars.substring(0,26);
  //    2: 9 = CWchars.substring(26,36);
  //    3: ? = CWchars.substring(36,45);
  //    4: <> = CWchars.substring(44,51);
  //    5: a9 = CWchars.substring(0,36);
  //    6: 9? = CWchars.substring(26,45);
  //    7: ?<> = CWchars.substring(36,51);
  //    8: a9? = CWchars.substring(0,45); 
  //    9: 9?<> = CWchars.substring(26,51);
  //  {OPT_ALL, OPT_ALPHA, OPT_NUM, OPT_PUNCT, OPT_PRO, OPT_ALNUM, OPT_NUMPUNCT, OPT_PUNCTPRO, OPT_ALNUMPUNCT, OPT_NUMPUNCTPRO}

String getRandomChars(int maxLength, int option) {  // random char string, 
    //eg. group of 5, 9 differing character pools; maxLength = 1-6
  String result = ""; 
  result.reserve(7);
  int s = 0, e = 53;
  int i;

  if (maxLength > 6) {  // we use a random length!
    maxLength = random(2, maxLength - 3);  // maxLength is max 10, 
      // so random upper limit is 7, means max 6 chars...
  }
//  if (kochActive) {
//    if (option == OPT_KOCH_ADAPTIVE)
//      return koch.getAdaptiveChar(maxLength);
//    else
//      return koch.getRandomChar(maxLength);
//  } else {
  switch (option) {  // set start of chars
    case OPT_NUM: 
    case OPT_NUMPUNCT: 
    case OPT_NUMPUNCTPRO: 
                          s = 26; break;
    case OPT_PUNCT: 
    case OPT_PUNCTPRO: 
                          s = 36; break;
    case OPT_PRO: 
                          s = 47; break;
    default:              s = 0;  break;
  }
  switch (option) {  // set end of chars
    case OPT_ALPHA: 
                          e = 26;  break;
    case OPT_ALNUM: 
    case OPT_NUM: 
                          e = 36; break;
    case OPT_ALNUMPUNCT: 
    case OPT_NUMPUNCT:
    case OPT_PUNCT: 
                          e = 47; break;
    default:              e = 53; break;
  }

  for (i = 0; i < maxLength; ++i) 
    result += CWchars.charAt(random(s,e));
//  }
  return result;
}


String getRandomAbbrev( int maxLength) {  // get a random CW abbreviation
  // max maxLength chars long (1-5) - 0 returns any length
  if (maxLength > 6) { maxLength = 0; }
//  if (kochActive)
//    return koch.getRandomAbbrev();
//  else
    return Abbrev::abbreviations[random(Abbrev::ABBREV_POINTER[maxLength], Abbrev::ABBREV_NUMBER_OF_ELEMENTS)];  
}


String getRandomWord( int maxLength) {  // give me a random English word
  // max maxLength chars long (1-5) - 0 returns any length
  if (maxLength > 6)
    maxLength = 0;
//  if (kochActive)
//    return koch.getRandomWord(); 
//  else 
    return EnglishWords::words[random(EnglishWords::WORDS_POINTER[maxLength], EnglishWords::WORDS_NUMBER_OF_ELEMENTS)];
}


unsigned int wordDoublerICS() {
  if (systemState != CWGENERATOR || wordDoubler == 0 || 
    ( wordDoubler != 0 && firstTime == false))
    return interCharacterSpace;
    
  switch (wordDoubler) {
    case 1: 
      return interCharacterSpace;
      break;
    case 2: 
      return halfICS;
      break;
    default: 
      return 3*ditLength;
      break;
  }
}


void openCorrectFile(int FileNo) {

  if (FileNo != fileNumber) {  // open correct file
    if (fileNumber != 0) {  // close old file first
      file.close();  
      // fileWordPointer values will be saved when returning to CWGENERATOR menus
    }
    // open correct file
    switch (FileNo) {
      case 1:  // open RealQSOs
        file = SPIFFS.open("/RealQSOs.txt");
        fileWordPointer = fileWdPointer1;
        break;
      case 2:  // open 2 word phrases
        file = SPIFFS.open("/phrases_2_words.txt");
        fileWordPointer = fileWdPointer2;
        break;
      case 3:  // open 3 word phrases
        file = SPIFFS.open("/phrases_3_words.txt");
        fileWordPointer = fileWdPointer3;
        break;
      case 4:  // open 4 word phrases
        file = SPIFFS.open("/phrases_4_words.txt");
        fileWordPointer = fileWdPointer4;
        break;
      default:
        break;
    }
  // move to correct position in file
  skipWords(fileWordPointer);             // fileWordPointer got incremented in here
  fileWordPointer = fileWordPointer / 2;  // set it back to correct value
  fileNumber = FileNo;
  }
}  // end openCorrectFile


String getWord() {  // get a word from the open file
  String result = "";
  byte c;
  static boolean eof = false;

  if (eof) {          // at eof return empty string
    eof = false;
    return result;
  }
  while (file.available()) {
    c=file.read();
    if (!isSpace(c))
      result += (char) c;
    else if (result.length() > 0)    {  // end of word
      fileWordPointer++;
      return result;
    }
  } // here eof
  eof = true;
  // reopen to move pointer to start of file
  file.close(); 
  switch (fileNumber) {
    case 1:  // open RealQSOs
      file = SPIFFS.open("/RealQSOs.txt");
      break;
    case 2:  // open 2 word phrases
      file = SPIFFS.open("/phrases_2_words.txt");
      break;
    case 3:  // open 3 word phrases
      file = SPIFFS.open("/phrases_3_words.txt");
      break;
    case 4:  // open 4 word phrases
      file = SPIFFS.open("/phrases_4_words.txt");
      break;
    default:
      break;
  }
  fileWordPointer = 0;
  while (!file.available())
    ;  // wait for file to be ready 
  return result;  // return last read word                          
}


void skipWords(uint32_t count) {  // skip count words in open file
  while (count > 0) {
    getWord();
    --count;
  }
}


void echoTrainerEval() {  // eval echo trainer response

  int i;
  delay(interCharacterSpace / 2);

  if (echoResponse.endsWith("r")) {
    echoResponse = "";
    echoTrainerState = COMPLETE_ANSWER;
    return;
  }
//  } else if ((i = echoResponse.indexOf("r")) != -1) {
//    echoResponse = echoResponse.substring(i+1);
//  }

//  Serial.println("echoResponse " + echoResponse);  // for debug
  if (echoResponse == echoTrainerWord) {  // response matched
    echoTrainerState = SEND_WORD;
    displayGeneratedMorse("ok");
    Serial.println(" ok");
    if (echoConf) { soundSignalOK(); }
//    if (kochActive){
//      koch.decreaseWordProbability(echoTrainerWord);
//    }
    delay(interWordSpace);
    if (speedAdapt) { changeSpeed(1); }
  } else {  // response does not match
    echoTrainerState = REPEAT_WORD;
    if (generatorMode != KOCH_LEARN || echoResponse != "") {
      ++errCounter;
      displayGeneratedMorse("err");
      Serial.println(" err");
      if (echoConf) { soundSignalERR(); }
//      if (kochActive){
//        koch.increaseWordProbability(echoTrainerWord, echoResponse);
//      }
    }
    delay(interWordSpace);
    if (speedAdapt) { changeSpeed(-1); }
  }
  echoResponse = "";
  clearPaddleLatches();

}  // end of function


void soundSignalOK() {
    keyOut(true, 440, volume);  // turn on tone
    delay(97);
    keyOut(false, 440, volume);  // turn off tone
    keyOut(true, 587, volume);  // turn on tone
    delay(193);
    keyOut(false, 587, volume);  // turn off tone
}


void soundSignalERR() {
    keyOut(true, 311, volume);  // turn on tone
    delay(193);
    keyOut(false, 311, volume);  // turn off tone
}


void changeSpeed(int t) {
  wpm += t;
  wpm = constrain(wpm, 5, 30);
  updateTimings();
  displayCWspeed();                     // update display of CW speed
//  charCounter = 0;                                    // reset character counter
}


void displayCWspeed() {
  int X = tft.getCursorX();
  int Y = tft.getCursorY();
  blankLine(2);
  displayLine(2, "WPM set to " + String(wpm), 0);
  tft.setCursor(X,Y);  
  delay(500);
}


// end keyer stuff


/**********************************************************/

// preferences stuff

prefAllNames prefWorking[20];

void processPref(boolean prefInit) {
  // use systemState to determine what prefs list to use
  static int prefPicked;
  static int prefItemCount = 0;
  static boolean pickingPrefName = true;  // true means pick pref name, not pref value
  static boolean recordOrigValue = true;  // true means remember original value

  if (prefInit) {  // get first preference
    switch (systemState) {
      case SETTINGS:
        prefItemCount = SizeOfArray(prefSETTINGS);
        for (int i; i<prefItemCount; i++) {
          prefWorking[i] = prefSETTINGS[i];
        }
        break;
      case CWKEYER:
        prefItemCount = SizeOfArray(prefCWKEYER);
        for (int i; i<prefItemCount; i++) {
          prefWorking[i] = prefCWKEYER[i];
        }
        break;
      case CWGENERATOR:
        prefItemCount = SizeOfArray(prefCWGENERATOR);
        for (int i; i<prefItemCount; i++) {
          prefWorking[i] = prefCWGENERATOR[i];
        }
        break;
      case ECHOTRAINER:
        prefItemCount = SizeOfArray(prefECHOTRAINER);
        for (int i; i<prefItemCount; i++) {
          prefWorking[i] = prefECHOTRAINER[i];
        }
        break;
      case KOCHTRAINER:
        prefItemCount = SizeOfArray(prefKOCHTRAINER);
        for (int i; i<prefItemCount; i++) {
          prefWorking[i] = prefKOCHTRAINER[i];
        }
        break;
      default:
        prefItemCount = SizeOfArray(prefSETTINGS);
        for (int i; i<prefItemCount; i++) {
          prefWorking[i] = prefSETTINGS[i];
        }
        break;
    }
    prefPicked = 0;
    pickingPrefName = true;
  }

  //  process if changing which preference to work with
  if (pickingPrefName) {
    switch (buttonPressed) {
      case UPSHORT:  // scroll pref name up
        prefPicked--;
        if (prefPicked < 0) { prefPicked = 0; }
        break;
      case DOWNSHORT:  // scroll pref name down
        prefPicked++;
        if (prefPicked > prefItemCount-1) { prefPicked = prefItemCount-1; }
        break;
      case UPLONG:  // select pref name for changing value
        pickingPrefName = false;
        break;
      case DOWNLONG:  // finished with pref stuff
        buttonPressed = NONE;
        savePreferences();  // save changed values in NVS
        taskStep = 0;  // go back to first task step for current systemState
        displayLine(5, "      Exit Settings", 0);
        delay(1000);
        if (systemState == SETTINGS) {  // exit SETTINGS tool
          systemState = START;
          taskStep = 1;
        }
        return;
        break;
      default:
        break;
    }
    buttonPressed = NONE;
    recordOrigValue = true;
  }

  // if pickingPrefName is true then scroll through pref names
  // if pickingPrefName is false then scroll through selected pref values
  switch (prefWorking[prefPicked]) {

    case prefKeyerMode:  // keyer mode
      static uint8_t OrigVal0;
      static uint8_t NewVal0;
      if (recordOrigValue) {
        recordOrigValue = false;
        OrigVal0 = keyermode;
        NewVal0 = OrigVal0;
      }
      switch (buttonPressed) {
        case UPSHORT:  // scroll up
          NewVal0--;  
          NewVal0 = constrain(NewVal0, 1, 5); 
          break;        
        case DOWNSHORT:  // scroll down
          NewVal0++;  
          NewVal0 = constrain(NewVal0, 1, 5); 
          break;
        case UPLONG:  // return keeping selected value
          keyermode = NewVal0;
          displayLine(5, "    Change Saved", 0);
          delay(1000);
          pickingPrefName = true;
          break;
        case DOWNLONG:  // return keeping original value
          NewVal0 = OrigVal0;
          displayLine(5, "  Change Not Saved", 0);
          delay(1000);
          pickingPrefName = true;
          break;
        default:
          break;
      }
      displayLine(3, "Keyer Mode", pickingPrefName);
      displayLine(4, keyerModes[NewVal0-1], !pickingPrefName);
      blankLine(5);
      break;

    case prefWPM:  // Words per Minute
      static int OrigVal1;
      static int NewVal1;
      if (recordOrigValue) {
        recordOrigValue = false;
        OrigVal1 = wpm;
        NewVal1 = OrigVal1;
      }
      switch (buttonPressed) {
        case UPSHORT:  // scroll up
          NewVal1++;  
          NewVal1 = constrain(NewVal1, 5, 30); 
          break;        
        case DOWNSHORT:  // scroll down
          NewVal1--;  
          NewVal1 = constrain(NewVal1, 5, 30); 
          break;
        case UPLONG:  // return keeping selected value
          wpm = NewVal1;
          updateTimings();
          displayLine(5, "    Change Saved", 0);
          delay(1000);
          pickingPrefName = true;
          break;
        case DOWNLONG:  // return keeping original value
          NewVal1 = OrigVal1;
          displayLine(5, "  Change Not Saved", 0);
          delay(1000);
          pickingPrefName = true;
          break;
        default:
          break;
      }
      displayLine(3, "WPM", pickingPrefName);
      displayLine(4, String(NewVal1), !pickingPrefName);
      blankLine(5);
      break;

    case prefPitch:  // tone/frequency in Hz 
      static int OrigVal2;
      static int NewVal2;
      if (recordOrigValue) {
        recordOrigValue = false;
        OrigVal2 = toneFreq;
        NewVal2 = OrigVal2;
      }
      switch (buttonPressed) {
        case UPSHORT:  // scroll up
          if (NewVal2 < 7) { NewVal2++; }  
          break;        
        case DOWNSHORT:  // scroll down
          if (NewVal2 > 0) { NewVal2--; }  
          break;
        case UPLONG:  // return keeping selected value
          toneFreq = NewVal2;
          displayLine(5, "    Change Saved", 0);
          delay(1000);
          pickingPrefName = true;
          break;
        case DOWNLONG:  // return keeping original value
          NewVal2 = OrigVal2;
          displayLine(5, "  Change Not Saved", 0);
          delay(1000);
          pickingPrefName = true;
          break;
        default:
          break;
      }
      if (!pickingPrefName) {
        keyOut(true, notes[NewVal2], volume);  // turn on tone
        delay(dahLength);
        keyOut(false, notes[NewVal2], volume);  // turn off tone
      }
      displayLine(3, "Tone/Pitch", pickingPrefName);
      displayLine(4, String(notes[NewVal2]) + " Hz", !pickingPrefName);
      blankLine(5);
      break;

    case prefPolarity:  // paddle polarity - dot/dash or dash/dot
      static boolean OrigVal3;
      static boolean NewVal3;
      if (recordOrigValue) {
        recordOrigValue = false;
        OrigVal3 = didah;
        NewVal3 = OrigVal3;
      }
      switch (buttonPressed) {
        case UPSHORT:  // scroll up
          NewVal3 = true; 
          break;        
        case DOWNSHORT:  // scroll down
          NewVal3 = false;
          break;
        case UPLONG:  // return keeping selected value
          didah = NewVal3;
          displayLine(5, "    Change Saved", 0);
          delay(1000);
          pickingPrefName = true;
          break;
        case DOWNLONG:  // return keeping original value
          NewVal3 = OrigVal3;
          displayLine(5, "  Change Not Saved", 0);
          delay(1000);
          pickingPrefName = true;
          break;
        default:
          break;
      }
      displayLine(3, "Polarity", pickingPrefName);
      if (NewVal3) {
        displayLine(4, "L - dot, R - dash", !pickingPrefName);
      } else {
        displayLine(4, "L - dash, R - dot", !pickingPrefName);
      }
      blankLine(5);
      break;

    case prefLatency:  // time span after element where next touch is not checked
      static uint8_t OrigVal4;
      static uint8_t NewVal4;
      if (recordOrigValue) {
        recordOrigValue = false;
        OrigVal4 = latency;
        NewVal4 = OrigVal4;
      }
      switch (buttonPressed) {
        case UPSHORT:  // scroll up
          if (NewVal4 < 7)  { NewVal4++; }  
          break;        
        case DOWNSHORT:  // scroll down
          if (NewVal4 > 0) { NewVal4--; } 
          break;
        case UPLONG:  // return keeping selected value
          latency = NewVal4;
          displayLine(5, "    Change Saved", 0);
          delay(1000);
          pickingPrefName = true;
          break;
        case DOWNLONG:  // return keeping original value
          NewVal4 = OrigVal4;
          displayLine(5, "  Change Not Saved", 0);
          delay(1000);
          pickingPrefName = true;
          break;
        default:
          break;
      }
      displayLine(3, "Latency", pickingPrefName);
      displayLine(4, String(NewVal4) + "/8 of a dot", !pickingPrefName);
      blankLine(5);
      break;

    case prefCurtisBDotT:  // timing for enhanced Curtis mode
      static uint8_t OrigVal5;
      static uint8_t NewVal5;
      if (recordOrigValue) {
        recordOrigValue = false;
        OrigVal5 = curtisBDotTimng;
        NewVal5 = OrigVal5;
      }
      switch (buttonPressed) {
        case UPSHORT:  // scroll up
          if (NewVal5 < 100)  { NewVal5 = NewVal5 + 5; }  
          break;        
        case DOWNSHORT:  // scroll down
          if (NewVal5 > 0) { NewVal5 = NewVal5 - 5; } 
          break;
        case UPLONG:  // return keeping selected value
          curtisBDotTimng = NewVal5;
          displayLine(5, "    Change Saved", 0);
          delay(1000);
          pickingPrefName = true;
          break;
        case DOWNLONG:  // return keeping original value
          NewVal5 = OrigVal5;
          displayLine(5, "  Change Not Saved", 0);
          delay(1000);
          pickingPrefName = true;
          break;
        default:
          break;
      }
      displayLine(3, "Curtis B Dot Timing", pickingPrefName);
      displayLine(4, String(NewVal5) + "%", !pickingPrefName);
      blankLine(5);
      break;

    case prefCurtisBDashT:  // timing for enhanced Curtis mode
      static uint8_t OrigVal6;
      static uint8_t NewVal6;
      if (recordOrigValue) {
        recordOrigValue = false;
        OrigVal6 = curtisBTiming;
        NewVal6 = OrigVal6;
      }
      switch (buttonPressed) {
        case UPSHORT:  // scroll up
          if (NewVal6 < 100)  { NewVal6 = NewVal6 + 5; }  
          break;        
        case DOWNSHORT:  // scroll down
          if (NewVal6 > 0) { NewVal6 = NewVal6 - 5; } 
          break;
        case UPLONG:  // return keeping selected value
          curtisBTiming = NewVal6;
          displayLine(5, "    Change Saved", 0);
          delay(1000);
          pickingPrefName = true;
          break;
        case DOWNLONG:  // return keeping original value
          NewVal6 = OrigVal6;
          displayLine(5, "  Change Not Saved", 0);
          delay(1000);
          pickingPrefName = true;
          break;
        default:
          break;
      }
      displayLine(3, "Curtis B Dash Timing", pickingPrefName);
      displayLine(4, String(NewVal6) + "%", !pickingPrefName);
      blankLine(5);
      break;

    case prefAutoCharSpace:  // auto character spacing
      static uint8_t OrigVal7;
      static uint8_t NewVal7;
      if (recordOrigValue) {
        recordOrigValue = false;
        OrigVal7 = ACSlength;
        NewVal7 = OrigVal7;
      }
      switch (buttonPressed) {
        case UPSHORT:  // scroll up
          if (NewVal7 == 0) { NewVal7 = 2; }
          else if (NewVal7 == 2) { NewVal7 = 3; }
          else if (NewVal7 == 3) { NewVal7 = 4; }
          else { NewVal7 = 4; }
          break;        
        case DOWNSHORT:  // scroll down
          if (NewVal7 == 4) { NewVal7 = 3; }
          else if (NewVal7 == 3) { NewVal7 = 2; }
          else if (NewVal7 == 2) { NewVal7 = 0; }
          else { NewVal7 = 0; }
          break;
        case UPLONG:  // return keeping selected value
          ACSlength = NewVal7;
          displayLine(5, "    Change Saved", 0);
          delay(1000);
          pickingPrefName = true;
          break;
        case DOWNLONG:  // return keeping original value
          NewVal7 = OrigVal7;
          displayLine(5, "  Change Not Saved", 0);
          delay(1000);
          pickingPrefName = true;
          break;
        default:
          break;
      }
      displayLine(3, "Auto Char Space", pickingPrefName);
      if (NewVal7 == 0) {
        displayLine(4, "Off", !pickingPrefName);
      } else {
        displayLine(4, String(NewVal7) + " dots", !pickingPrefName);
      }
      blankLine(5);
      break;

    case prefStopNextRep:  // stop after each word
      static boolean OrigVal8;
      static boolean NewVal8;
      if (recordOrigValue) {
        recordOrigValue = false;
        OrigVal8 = autoStopMode;
        NewVal8 = OrigVal8;
      }
      switch (buttonPressed) {
        case UPSHORT:  // scroll up
          NewVal8 = true; 
          break;        
        case DOWNSHORT:  // scroll down
          NewVal8 = false;
          break;
        case UPLONG:  // return keeping selected value
          autoStopMode = NewVal8;
          if (NewVal8) { wordDoubler = 0; }  // turn off word doubler
          displayLine(5, "    Change Saved", 0);
          delay(1000);
          pickingPrefName = true;
          break;
        case DOWNLONG:  // return keeping original value
          NewVal8 = OrigVal8;
          displayLine(5, "  Change Not Saved", 0);
          delay(1000);
          pickingPrefName = true;
          break;
        default:
          break;
      }
      displayLine(3, "Stop/Next/Repeat", pickingPrefName);
      if (NewVal8) {
        displayLine(4, "On", !pickingPrefName);
      } else {
        displayLine(4, "Off", !pickingPrefName);
      }
      blankLine(5);
      break;

    case prefWordDoubler:  // word doubler
      static uint8_t OrigVal9;
      static uint8_t NewVal9;
      if (recordOrigValue) {
        recordOrigValue = false;
        OrigVal9 = wordDoubler;
        NewVal9 = OrigVal9;
      }
      switch (buttonPressed) {
        case UPSHORT:  // scroll up
          if (NewVal9 < 3) { NewVal9++; }
          break;        
        case DOWNSHORT:  // scroll down
          if (NewVal9 > 0) { NewVal9--; } 
          break;
        case UPLONG:  // return keeping selected value
          wordDoubler = NewVal9;
          if (NewVal9) { autoStopMode = false; }  // turn off autoStopMode
          displayLine(5, "    Change Saved", 0);
          delay(1000);
          pickingPrefName = true;
          break;
        case DOWNLONG:  // return keeping original value
          NewVal9 = OrigVal9;
          displayLine(5, "  Change Not Saved", 0);
          delay(1000);
          pickingPrefName = true;
          break;
        default:
          break;
      }
      displayLine(3, "Each Word 2x", pickingPrefName);
      if (NewVal9 == 3) {
        displayLine(4, "On (true WPM)", !pickingPrefName);
      } else if (NewVal9 == 2) {
        displayLine(4, "On (less ICS)", !pickingPrefName);
      } else if (NewVal9 == 1) {
        displayLine(4, "On", !pickingPrefName);
      } else {
        displayLine(4, "Off", !pickingPrefName);
      }
      blankLine(5);
      break;

    case prefMaxSequence:  // number of words before pausing
      static uint8_t OrigVal10;
      static uint8_t NewVal10;
      if (recordOrigValue) {
        recordOrigValue = false;
        OrigVal10 = maxSequence;
        NewVal10 = OrigVal10;
      }
      switch (buttonPressed) {
        case UPSHORT:  // scroll up
          if (NewVal10 < 100) { NewVal10 = NewVal10 + 5; }
          break;        
        case DOWNSHORT:  // scroll down
          if (NewVal10 > 0) { NewVal10 = NewVal10 - 5; } 
          break;
        case UPLONG:  // return keeping selected value
          maxSequence = NewVal10;
          displayLine(5, "    Change Saved", 0);
          delay(1000);
          pickingPrefName = true;
          break;
        case DOWNLONG:  // return keeping original value
          NewVal10 = OrigVal10;
          displayLine(5, "  Change Not Saved", 0);
          delay(1000);
          pickingPrefName = true;
          break;
        default:
          break;
      }
      displayLine(3, "Max # of words", pickingPrefName);
      if (NewVal10 == 0) {
        displayLine(4, "Unlimited", !pickingPrefName);
      } else {
        displayLine(4, String(NewVal10) + " words", !pickingPrefName);
      }
      blankLine(5);
      break;

    case prefInterWordSpace:  // space between words in dots
      static int OrigVal11;
      static int NewVal11;
      if (recordOrigValue) {
        recordOrigValue = false;
        OrigVal11 = interWdSpace;
        NewVal11 = OrigVal11;
      }
      switch (buttonPressed) {
        case UPSHORT:  // scroll up
          if (NewVal11 < 45) { NewVal11++; }
          break;        
        case DOWNSHORT:  // scroll down
          if (NewVal11 > 6) { NewVal11--; } 
          break;
        case UPLONG:  // return keeping selected value
          interWdSpace = NewVal11;
          displayLine(5, "    Change Saved", 0);
          delay(1000);
          pickingPrefName = true;
          updateTimings();
          break;
        case DOWNLONG:  // return keeping original value
          NewVal11 = OrigVal11;
          displayLine(5, "  Change Not Saved", 0);
          delay(1000);
          pickingPrefName = true;
          break;
        default:
          break;
      }
      displayLine(3, "Interword space", pickingPrefName);
      displayLine(4, String(NewVal11) + " dots", !pickingPrefName);
      blankLine(5);
      break;

    case prefInterCharSpace:  // space between characters in dots
      static uint8_t OrigVal12;
      static uint8_t NewVal12;
      if (recordOrigValue) {
        recordOrigValue = false;
        OrigVal12 = interCharSpace;
        NewVal12 = OrigVal12;
      }
      switch (buttonPressed) {
        case UPSHORT:  // scroll up
          if (NewVal12 < 15) { NewVal12++; }
          break;        
        case DOWNSHORT:  // scroll down
          if (NewVal12 > 3) { NewVal12--; } 
          break;
        case UPLONG:  // return keeping selected value
          interCharSpace = NewVal12;
          displayLine(5, "    Change Saved", 0);
          delay(1000);
          pickingPrefName = true;
          updateTimings();
          break;
        case DOWNLONG:  // return keeping original value
          NewVal12 = OrigVal12;
          displayLine(5, "  Change Not Saved", 0);
          delay(1000);
          pickingPrefName = true;
          break;
        default:
          break;
      }
      displayLine(3, "Interchar space", pickingPrefName);
      displayLine(4, String(NewVal12) + " dots", !pickingPrefName);
      blankLine(5);
      break;

    case prefRandomOption:  // choose pool to get chars from 
      static uint8_t OrigVal13;
      static uint8_t NewVal13;
      if (recordOrigValue) {
        recordOrigValue = false;
        OrigVal13 = randomOption;
        NewVal13 = OrigVal13;
      }
      switch (buttonPressed) {
        case UPSHORT:  // scroll up
          if (NewVal13 > 0) { NewVal13--; }
          break;        
        case DOWNSHORT:  // scroll down
          if (NewVal13 < 9) { NewVal13++; } 
          break;
        case UPLONG:  // return keeping selected value
          randomOption = NewVal13;
          displayLine(5, "    Change Saved", 0);
          delay(1000);
          pickingPrefName = true;
          break;
        case DOWNLONG:  // return keeping original value
          NewVal13 = OrigVal13;
          displayLine(5, "  Change Not Saved", 0);
          delay(1000);
          pickingPrefName = true;
          break;
        default:
          break;
      }
      displayLine(3, "Random Option", pickingPrefName);
      if (NewVal13 == 1) { displayLine(4, "Alpha", !pickingPrefName); }
      else if (NewVal13 == 2) { displayLine(4, "Numerals", !pickingPrefName); }
      else if (NewVal13 == 3) { displayLine(4, "Punctuation", !pickingPrefName); }
      else if (NewVal13 == 4) { displayLine(4, "Pro Signs", !pickingPrefName); }
      else if (NewVal13 == 5) { displayLine(4, "Alpha + Numurals", !pickingPrefName); }
      else if (NewVal13 == 6) { displayLine(4, "Numerals + Punct", !pickingPrefName); }
      else if (NewVal13 == 7) { displayLine(4, "Punct + Pro Signs", !pickingPrefName); }
      else if (NewVal13 == 8) { displayLine(4, "Alpha+Num+Punct", !pickingPrefName); }
      else if (NewVal13 == 9) { displayLine(4, "Num+Punct+ProSigns", !pickingPrefName); }
      else { displayLine(4, "All Chars", !pickingPrefName); }
      blankLine(5);
      break;

    case prefRandomLength:  // how many chars in a group/word
      static uint8_t OrigVal14;
      static uint8_t NewVal14;
      if (recordOrigValue) {
        recordOrigValue = false;
        OrigVal14 = randomLength;
        NewVal14 = OrigVal14;
      }
      switch (buttonPressed) {
        case UPSHORT:  // scroll up
          if (NewVal14 < 5) { NewVal14++; }
          break;        
        case DOWNSHORT:  // scroll down
          if (NewVal14 > 1) { NewVal14--; } 
          break;
        case UPLONG:  // return keeping selected value
          randomLength = NewVal14;
          displayLine(5, "    Change Saved", 0);
          delay(1000);
          pickingPrefName = true;
          break;
        case DOWNLONG:  // return keeping original value
          NewVal14 = OrigVal14;
          displayLine(5, "  Change Not Saved", 0);
          delay(1000);
          pickingPrefName = true;
          break;
        default:
          break;
      }
      displayLine(3, "Random Length", pickingPrefName);
      displayLine(4, String(NewVal14) + " characters", !pickingPrefName);
      blankLine(5);
      break;

    case prefAbbrWdLength:  // max length of abbreviations or words
      static uint8_t OrigVal15;
      static uint8_t NewVal15;
      if (recordOrigValue) {
        recordOrigValue = false;
        OrigVal15 = wordLength;
        NewVal15 = OrigVal15;
      }
      switch (buttonPressed) {
        case UPSHORT:  // scroll up
          if (NewVal15 < 6) { NewVal15++; }
          if (NewVal15 == 1) { NewVal15++; }
          break;        
        case DOWNSHORT:  // scroll down
          if (NewVal15 > 1) { NewVal15--; } 
          if (NewVal15 == 1) { NewVal15--; }
          break;
        case UPLONG:  // return keeping selected value
          abbrevLength = NewVal15;
          wordLength = NewVal15;
          displayLine(5, "    Change Saved", 0);
          delay(1000);
          pickingPrefName = true;
          break;
        case DOWNLONG:  // return keeping original value
          NewVal15 = OrigVal15;
          displayLine(5, "  Change Not Saved", 0);
          delay(1000);
          pickingPrefName = true;
          break;
        default:
          break;
      }
      displayLine(3, "Abbr/Word Mx Length", pickingPrefName);
      if (NewVal15 == 0) {
        displayLine(4, "Unlimited", !pickingPrefName);
      } else {
        displayLine(4, String(NewVal15) + " characters", !pickingPrefName);
      }
      blankLine(5);
      break;

    case prefCWGenDisplay:  // display none, by char, or by word
      static uint8_t OrigVal16;
      static uint8_t NewVal16;
      if (recordOrigValue) {
        recordOrigValue = false;
        OrigVal16 = trainerDisplay;
        NewVal16 = OrigVal16;
      }
      switch (buttonPressed) {
        case UPSHORT:  // scroll up
          if (NewVal16 < 2) { NewVal16++; }
          break;        
        case DOWNSHORT:  // scroll down
          if (NewVal16 > 0) { NewVal16--; } 
          break;
        case UPLONG:  // return keeping selected value
          trainerDisplay = NewVal16;
          displayLine(5, "    Change Saved", 0);
          delay(1000);
          pickingPrefName = true;
          break;
        case DOWNLONG:  // return keeping original value
          NewVal16 = OrigVal16;
          displayLine(5, "  Change Not Saved", 0);
          delay(1000);
          pickingPrefName = true;
          break;
        default:
          break;
      }
      displayLine(3, "CW Gen Display", pickingPrefName);
      if (NewVal16 == 2) {
        displayLine(4, "Word by word", !pickingPrefName);
      } else if (NewVal16 == 1) {
        displayLine(4, "Char by char", !pickingPrefName);
      } else {
        displayLine(4, "Display off", !pickingPrefName);
      }
      blankLine(5);
      break;

    case prefEchoDisplay:  // code only, chars only, codes and chars
      static uint8_t OrigVal17;
      static uint8_t NewVal17;
      if (recordOrigValue) {
        recordOrigValue = false;
        OrigVal17 = echoDisplay;
        NewVal17 = OrigVal17;
      }
      switch (buttonPressed) {
        case UPSHORT:  // scroll up
          if (NewVal17 < 3) { NewVal17++; }
          break;        
        case DOWNSHORT:  // scroll down
          if (NewVal17 > 0) { NewVal17--; } 
          break;
        case UPLONG:  // return keeping selected value
          echoDisplay = NewVal17;
          displayLine(5, "    Change Saved", 0);
          delay(1000);
          pickingPrefName = true;
          break;
        case DOWNLONG:  // return keeping original value
          NewVal17 = OrigVal17;
          displayLine(5, "  Change Not Saved", 0);
          delay(1000);
          pickingPrefName = true;
          break;
        default:
          break;
      }
      displayLine(3, "Echo Display", pickingPrefName);
      if (NewVal17 == 3) {
        displayLine(4, "Code and Display", !pickingPrefName);
      } else if (NewVal17 == 2) {
        displayLine(4, "Display Only", !pickingPrefName);
      } else if (NewVal17 == 1) {
        displayLine(4, "Code Only", !pickingPrefName);
      } else {
        displayLine(4, "No Prompt", !pickingPrefName);
      }
      blankLine(5);
      break;

    case prefWordPause:  // pause after words in seconds
      static uint8_t OrigVal18;
      static uint8_t NewVal18;
      if (recordOrigValue) {
        recordOrigValue = false;
        OrigVal18 = wordPause;
        NewVal18 = OrigVal18;
      }
      switch (buttonPressed) {
        case UPSHORT:  // scroll up
          if (NewVal18 < 10) { NewVal18++; }
          break;        
        case DOWNSHORT:  // scroll down
          if (NewVal18 > 0) { NewVal18--; } 
          break;
        case UPLONG:  // return keeping selected value
          wordPause = NewVal18;
          displayLine(5, "    Change Saved", 0);
          delay(1000);
          pickingPrefName = true;
          break;
        case DOWNLONG:  // return keeping original value
          NewVal18 = OrigVal18;
          displayLine(5, "  Change Not Saved", 0);
          delay(1000);
          pickingPrefName = true;
          break;
        default:
          break;
      }
      displayLine(3, "Pause between words", pickingPrefName);
      if (NewVal18 == 0) {
        displayLine(4, "Normal interword", !pickingPrefName);
      } else {
        displayLine(4, String(NewVal18) + " seconds", !pickingPrefName);
      }
      blankLine(5);
      break;

    case prefVolume:  // volume 
      static int OrigVal19;
      static int NewVal19;
      if (recordOrigValue) {
        recordOrigValue = false;
        OrigVal19 = volume;
        NewVal19 = OrigVal19;
      }
      switch (buttonPressed) {
        case UPSHORT:  // scroll up
          if (NewVal19 < 10) { NewVal19++; }  
          break;        
        case DOWNSHORT:  // scroll down
          if (NewVal19 > 0) { NewVal19--; }  
          break;
        case UPLONG:  // return keeping selected value
          volume = NewVal19;
          displayLine(5, "    Change Saved", 0);
          delay(1000);
          pickingPrefName = true;
          break;
        case DOWNLONG:  // return keeping original value
          NewVal19 = OrigVal19;
          displayLine(5, "  Change Not Saved", 0);
          delay(1000);
          pickingPrefName = true;
          break;
        default:
          break;
      }
      if (!pickingPrefName) {
        keyOut(true, notes[toneFreq], NewVal19);  // turn on tone
        delay(dahLength);
        keyOut(false, notes[toneFreq], NewVal19);  // turn off tone
      }
      displayLine(3, "Volume", pickingPrefName);
      if (NewVal19 == 0) {
        displayLine(4, "0 (Off)", !pickingPrefName);
      } else if (NewVal19 == 10) {
        displayLine(4, "10 (Max)", !pickingPrefName);
      } else {
        displayLine(4, String(NewVal19), !pickingPrefName);
      }
      blankLine(5);
      break;

    case prefRandomFile:  // get random words from phrase files
      static uint8_t OrigVal20;
      static uint8_t NewVal20;
      if (recordOrigValue) {
        recordOrigValue = false;
        OrigVal20 = randomFile;
        NewVal20 = OrigVal20;
      }
      switch (buttonPressed) {
        case UPSHORT:  // scroll up
          NewVal20 = 255;
          break;        
        case DOWNSHORT:  // scroll down
          NewVal20 = 0; 
          break;
        case UPLONG:  // return keeping selected value
          randomFile = NewVal20;
          displayLine(5, "    Change Saved", 0);
          delay(1000);
          pickingPrefName = true;
          break;
        case DOWNLONG:  // return keeping original value
          NewVal20 = OrigVal20;
          displayLine(5, "  Change Not Saved", 0);
          delay(1000);
          pickingPrefName = true;
          break;
        default:
          break;
      }
      displayLine(3, "Random phrase word", pickingPrefName);
      if (NewVal20 == 0) {
        displayLine(4, "Off", !pickingPrefName);
      } else {
        displayLine(4, "On", !pickingPrefName);
      }
      blankLine(5);
      break;

    case prefToneShift:  // echo tone shift
      static uint8_t OrigVal21;
      static uint8_t NewVal21;
      if (recordOrigValue) {
        recordOrigValue = false;
        OrigVal21 = echoToneShift;
        NewVal21 = OrigVal21;
      }
      switch (buttonPressed) {
        case UPSHORT:  // scroll up
          if (NewVal21 == 0) { NewVal21 = 1; }
          else if (NewVal21 == 2) { NewVal21 = 0; }
          break;        
        case DOWNSHORT:  // scroll down
          if (NewVal21 == 0) { NewVal21 = 2; }
          else if (NewVal21 == 1) { NewVal21 = 0; }
          break;
        case UPLONG:  // return keeping selected value
          echoToneShift = NewVal21;
          displayLine(5, "    Change Saved", 0);
          delay(1000);
          pickingPrefName = true;
          break;
        case DOWNLONG:  // return keeping original value
          NewVal21 = OrigVal21;
          displayLine(5, "  Change Not Saved", 0);
          delay(1000);
          pickingPrefName = true;
          break;
        default:
          break;
      }
      displayLine(3, "Echo tone shift", pickingPrefName);
      if (NewVal21 == 0) {
        displayLine(4, "No shift", !pickingPrefName);
      } else if (NewVal21 == 1) {
        displayLine(4, "Up a half tone", !pickingPrefName);
      } else {
        displayLine(4, "Down a half tone", !pickingPrefName);
      }
      blankLine(5);
      break;

    case prefEchoRepeats:  // echo repeats before moving on
      static uint8_t OrigVal22;
      static uint8_t NewVal22;
      if (recordOrigValue) {
        recordOrigValue = false;
        OrigVal22 = echoRepeats;
        NewVal22 = OrigVal22;
      }
      switch (buttonPressed) {
        case UPSHORT:  // scroll up
          if (NewVal22 < 7) { NewVal22++; }
          break;        
        case DOWNSHORT:  // scroll down
          if (NewVal22 > 0) { NewVal22--; }
          break;
        case UPLONG:  // return keeping selected value
          echoRepeats = NewVal22;
          displayLine(5, "    Change Saved", 0);
          delay(1000);
          pickingPrefName = true;
          break;
        case DOWNLONG:  // return keeping original value
          NewVal22 = OrigVal22;
          displayLine(5, "  Change Not Saved", 0);
          delay(1000);
          pickingPrefName = true;
          break;
        default:
          break;
      }
      displayLine(3, "Echo repeats on err", pickingPrefName);
      displayLine(4, String(NewVal22), !pickingPrefName);
      blankLine(5);
      break;

    case prefPromptPause:  // delay before new echo word
      static uint8_t OrigVal23;
      static uint8_t NewVal23;
      if (recordOrigValue) {
        recordOrigValue = false;
        OrigVal23 = promptPause;
        NewVal23 = OrigVal23;
      }
      switch (buttonPressed) {
        case UPSHORT:  // scroll up
          if (NewVal23 < 12) { NewVal23++; }
          break;        
        case DOWNSHORT:  // scroll down
          if (NewVal23 > 2) { NewVal23--; }
          break;
        case UPLONG:  // return keeping selected value
          promptPause = NewVal23;
          displayLine(5, "    Change Saved", 0);
          delay(1000);
          pickingPrefName = true;
          break;
        case DOWNLONG:  // return keeping original value
          NewVal23 = OrigVal23;
          displayLine(5, "  Change Not Saved", 0);
          delay(1000);
          pickingPrefName = true;
          break;
        default:
          break;
      }
      displayLine(3, "Echo prompt pause", pickingPrefName);
      displayLine(4, String(NewVal23), !pickingPrefName);
      blankLine(5);
      break;

    case prefResponsePause:  // delay before new echo word
      static uint8_t OrigVal24;
      static uint8_t NewVal24;
      if (recordOrigValue) {
        recordOrigValue = false;
        OrigVal24 = responsePause;
        NewVal24 = OrigVal24;
      }
      switch (buttonPressed) {
        case UPSHORT:  // scroll up
          if (NewVal24 < 12) { NewVal24++; }
          break;        
        case DOWNSHORT:  // scroll down
          if (NewVal24 > 2) { NewVal24--; }
          break;
        case UPLONG:  // return keeping selected value
          responsePause = NewVal24;
          displayLine(5, "    Change Saved", 0);
          delay(1000);
          pickingPrefName = true;
          break;
        case DOWNLONG:  // return keeping original value
          NewVal24 = OrigVal24;
          displayLine(5, "  Change Not Saved", 0);
          delay(1000);
          pickingPrefName = true;
          break;
        default:
          break;
      }
      displayLine(3, "Echo response pause", pickingPrefName);
      displayLine(4, String(NewVal24), !pickingPrefName);
      blankLine(5);
      break;

    case prefEchoConf:  // echo audible ok/err
      static boolean OrigVal25;
      static boolean NewVal25;
      if (recordOrigValue) {
        recordOrigValue = false;
        OrigVal25 = echoConf;
        NewVal25 = OrigVal25;
      }
      switch (buttonPressed) {
        case UPSHORT:  // scroll up
          NewVal25 = true; 
          break;        
        case DOWNSHORT:  // scroll down
          NewVal25 = false;
          break;
        case UPLONG:  // return keeping selected value
          echoConf = NewVal25;
          displayLine(5, "    Change Saved", 0);
          delay(1000);
          pickingPrefName = true;
          break;
        case DOWNLONG:  // return keeping original value
          NewVal25 = OrigVal25;
          displayLine(5, "  Change Not Saved", 0);
          delay(1000);
          pickingPrefName = true;
          break;
        default:
          break;
      }
      displayLine(3, "Echo audible ok/err", pickingPrefName);
      if (NewVal25) {
        displayLine(4, "On", !pickingPrefName);
      } else {
        displayLine(4, "Off", !pickingPrefName);
      }
      blankLine(5);
      break;

    case prefSpeedAdapt:  // change echo speed if ok/err
      static boolean OrigVal26;
      static boolean NewVal26;
      if (recordOrigValue) {
        recordOrigValue = false;
        OrigVal26 = speedAdapt;
        NewVal26 = OrigVal26;
      }
      switch (buttonPressed) {
        case UPSHORT:  // scroll up
          NewVal26 = true; 
          break;        
        case DOWNSHORT:  // scroll down
          NewVal26 = false;
          break;
        case UPLONG:  // return keeping selected value
          speedAdapt = NewVal26;
          displayLine(5, "    Change Saved", 0);
          delay(1000);
          pickingPrefName = true;
          break;
        case DOWNLONG:  // return keeping original value
          NewVal26 = OrigVal26;
          displayLine(5, "  Change Not Saved", 0);
          delay(1000);
          pickingPrefName = true;
          break;
        default:
          break;
      }
      displayLine(3, "Echo speed adapt", pickingPrefName);
      if (NewVal26) {
        displayLine(4, "On", !pickingPrefName);
      } else {
        displayLine(4, "Off", !pickingPrefName);
      }
      blankLine(5);
      break;

    case prefShowScrollHelp:  // show scroll help on boot
      static boolean OrigVal27;
      static boolean NewVal27;
      if (recordOrigValue) {
        recordOrigValue = false;
        OrigVal27 = showScrollHelp;
        NewVal27 = OrigVal27;
      }
      switch (buttonPressed) {
        case UPSHORT:  // scroll up
          NewVal27 = true; 
          break;        
        case DOWNSHORT:  // scroll down
          NewVal27 = false;
          break;
        case UPLONG:  // return keeping selected value
          showScrollHelp = NewVal27;
          displayLine(5, "    Change Saved", 0);
          delay(1000);
          pickingPrefName = true;
          break;
        case DOWNLONG:  // return keeping original value
          NewVal27 = OrigVal27;
          displayLine(5, "  Change Not Saved", 0);
          delay(1000);
          pickingPrefName = true;
          break;
        default:
          break;
      }
      displayLine(3, "Show scroll help", pickingPrefName);
      if (NewVal27) {
        displayLine(4, "On", !pickingPrefName);
      } else {
        displayLine(4, "Off", !pickingPrefName);
      }
      blankLine(5);
      break;

    case prefKochOrder:  // choose Koch Order (KOCH, LCWO, CWAC, LICW)
      static uint8_t OrigVal28;
      static uint8_t NewVal28;
      if (recordOrigValue) {
        recordOrigValue = false;
        OrigVal28 = kochOrder;
        NewVal28 = OrigVal28;
      }
      switch (buttonPressed) {
        case UPSHORT:  // scroll up
          if (NewVal28 > 0) { NewVal28--; }
          break;        
        case DOWNSHORT:  // scroll down
          if (NewVal28 < 3) { NewVal28++; }
          break;
        case UPLONG:  // return keeping selected value
          kochOrder = NewVal28;
          displayLine(5, "    Change Saved", 0);
          delay(1000);
          pickingPrefName = true;
          break;
        case DOWNLONG:  // return keeping original value
          NewVal28 = OrigVal28;
          displayLine(5, "  Change Not Saved", 0);
          delay(1000);
          pickingPrefName = true;
          break;
        default:
          break;
      }
      displayLine(3, "Koch char order", pickingPrefName);
      switch (NewVal28) {
        case 0: displayLine(4, "Koch Order", !pickingPrefName); break;
        case 1: displayLine(4, "LCWO Order", !pickingPrefName); break;
        case 2: displayLine(4, "CW Academy Order", !pickingPrefName); break;
        case 3: displayLine(4, "LICW Order", !pickingPrefName); break;
      }
      blankLine(5);
      break;

    case prefKochLesson:  // choose Koch Lesson 0 - 50 (51 lessons)
      static uint8_t OrigVal29;
      static uint8_t NewVal29;
      if (recordOrigValue) {
        recordOrigValue = false;
        switch (kochOrder) {
          case 0: { OrigVal29 = kochLesson; break; }
          case 1: { OrigVal29 = lcwoLesson; break; }
          case 2: { OrigVal29 = cwacLesson; break; }
          case 3: { OrigVal29 = licwLesson; break; }
        }
        NewVal29 = OrigVal29;
      }
      switch (buttonPressed) {
        case UPSHORT:  // scroll up
          if (NewVal29 < 51) { NewVal29++; }
          break;        
        case DOWNSHORT:  // scroll down
          if (NewVal29 > 1) { NewVal29--; }
          break;
        case UPLONG:  // return keeping selected value
          switch (kochOrder) {
            case 0: { kochLesson = OrigVal29; break; }
            case 1: { lcwoLesson = OrigVal29; break; }
            case 2: { cwacLesson = OrigVal29; break; }
            case 3: { licwLesson = OrigVal29; break; }
          }
          displayLine(5, "    Change Saved", 0);
          delay(1000);
          pickingPrefName = true;
          break;
        case DOWNLONG:  // return keeping original value
          NewVal29 = OrigVal29;
          displayLine(5, "  Change Not Saved", 0);
          delay(1000);
          pickingPrefName = true;
          break;
        default:
          break;
      }
      switch (kochOrder) {
        case 0: { 
          displayLine(3, "Koch Lesson", pickingPrefName); 
          displayLine(4, String(NewVal29) + " of 51 (" + 
            cleanUpProSigns(String(KochChars[NewVal29-1])) + ")", !pickingPrefName);
          break;
        }
        case 1: {
          displayLine(3, "LCWO Lesson", pickingPrefName); 
          displayLine(4, String(NewVal29) + " of 51 (" + 
            cleanUpProSigns(String(LCWOChars[NewVal29-1])) + ")", !pickingPrefName);
          break;
        }
        case 2: {
          displayLine(3, "CW Academy Lesson", pickingPrefName); 
          displayLine(4, String(NewVal29) + " of 51 (" + 
            cleanUpProSigns(String(CWACChars[NewVal29-1])) + ")", !pickingPrefName);
          break;
        }
        case 3: {
          displayLine(3, "LICW Lesson", pickingPrefName); 
          displayLine(4, String(NewVal29) + " of 51 (" + 
            cleanUpProSigns(String(LICWChars[NewVal29-1])) + ")", !pickingPrefName);
          break;
        }
      }
      blankLine(5);
      break;

    case prefKochSingle:  // 0 - all chars up to lesson, 1 - just lesson char
      static uint8_t OrigVal30;
      static uint8_t NewVal30;
      if (recordOrigValue) {
        recordOrigValue = false;
        OrigVal30 = kochSingle;
        NewVal30 = OrigVal30;
      }
      switch (buttonPressed) {
        case UPSHORT:  // scroll up
          if (NewVal30 > 0) { NewVal30--; }
          break;        
        case DOWNSHORT:  // scroll down
          if (NewVal30 < 1) { NewVal30++; }
          break;
        case UPLONG:  // return keeping selected value
          kochSingle = NewVal30;
          displayLine(5, "    Change Saved", 0);
          delay(1000);
          pickingPrefName = true;
          break;
        case DOWNLONG:  // return keeping original value
          NewVal30 = OrigVal30;
          displayLine(5, "  Change Not Saved", 0);
          delay(1000);
          pickingPrefName = true;
          break;
        default:
          break;
      }
      displayLine(3, "Koch Chars", pickingPrefName);
      if (NewVal30) { 
        displayLine(4, "Only lesson " + String(kochLesson) + " char", !pickingPrefName); break;
      }
      else { 
        displayLine(4, "All up to lesson " + String(kochLesson), !pickingPrefName); break;
      }
      blankLine(5);
      break;

    case prefKochShowMorse:  // show scroll help on boot
      static boolean OrigVal31;
      static boolean NewVal31;
      if (recordOrigValue) {
        recordOrigValue = false;
        OrigVal31 = kochShowMorse;
        NewVal31 = OrigVal31;
      }
      switch (buttonPressed) {
        case UPSHORT:  // scroll up
          NewVal31 = true; 
          break;        
        case DOWNSHORT:  // scroll down
          NewVal31 = false;
          break;
        case UPLONG:  // return keeping selected value
          kochShowMorse = NewVal31;
          displayLine(5, "    Change Saved", 0);
          delay(1000);
          pickingPrefName = true;
          break;
        case DOWNLONG:  // return keeping original value
          NewVal31 = OrigVal31;
          displayLine(5, "  Change Not Saved", 0);
          delay(1000);
          pickingPrefName = true;
          break;
        default:
          break;
      }
      displayLine(3, "Koch show morse", pickingPrefName);
      if (NewVal31) {
        displayLine(4, "On", !pickingPrefName);
      } else {
        displayLine(4, "Off", !pickingPrefName);
      }
      blankLine(5);
      break;

    case prefKochCWTimer:  // how many seconds to generate Koch CW
      static uint8_t OrigVal32;
      static uint8_t NewVal32;
      if (recordOrigValue) {
        recordOrigValue = false;
        OrigVal32 = kochCWTimer;
        NewVal32 = OrigVal32;
      }
      switch (buttonPressed) {
        case UPSHORT:  // scroll up
          if (NewVal32 < 9) { NewVal32++; }
          break;        
        case DOWNSHORT:  // scroll down
          if (NewVal32 > 1) { NewVal32--; }
          break;
        case UPLONG:  // return keeping selected value
          kochCWTimer = NewVal32;
          displayLine(5, "    Change Saved", 0);
          delay(1000);
          pickingPrefName = true;
          break;
        case DOWNLONG:  // return keeping original value
          NewVal32 = OrigVal32;
          displayLine(5, "  Change Not Saved", 0);
          delay(1000);
          pickingPrefName = true;
          break;
        default:
          break;
      }
      displayLine(3, "Koch CW Timer", pickingPrefName);
      displayLine(4, String(NewVal32*10) + " seconds", !pickingPrefName); break;
      blankLine(5);
      break;

    default:
      break;
  }
  buttonPressed = NONE;
}

// end preferences stuff
