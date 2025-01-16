
// defines and functions for PracticeMorse

// keyer modes
#define    IAMBICA      1  // Curtis Mode A
#define    IAMBICB      2  // Curtis Mode B (with enhanced Curtis timing, 
                           // set as parameter
#define    ULTIMATIC    3  // Ultimatic mode
#define    NONSQUEEZE   4  // Non-squeeze mode of dual-lever paddles 
                           // simulate a single-lever paddle
#define    STRAIGHTKEY  5  // use of a straight key (for echo training etc) 
                           // not really a "keyer" mode

const int notes[] = {523, 587, 659, 698, 784, 880, 988, 1047};

  // these will be saved in NVS
  // they should have default values for first time write to NVS

  boolean showScrollHelp = true;  // show scroll help dialog on boot
  boolean didah = true;  // paddle polarity - true dit left dah right
  uint8_t keyermode = 2;                    // Iambic keyer mode: see the #defines in morsedefs.h
  uint8_t interCharSpace = 3;               // trainer: in dit lengths                          3 - 24
  boolean reversePolarity = false;          // has now a different meaning: true when we need to reverse the polarity of the ext paddle
  uint8_t ACSlength = 0;                    // in ACS: we extend the pause between charcaters to the equal length of how many dots 
                                              // (2, 3 or 4 are meaningful, 0 means off) 0, 2-4
  int toneFreq = 4;  // frequency selection from notes[]
  int volume = 5;  // 0 - 10, 0 is off, default 5
//  boolean encoderClicks = true;             // all: should rotating the encoder generate a click?
  uint8_t randomLength = 3;                 // trainer: how many random chars in one group -    1 -  5
  uint8_t randomOption = 0;                 // trainer: from which pool are we generating random characters?  0 - 9
//  uint8_t callLength = 0;                   // trainer: max length of call signs generated (0 = unlimited)    0, 3 - 6
  uint8_t abbrevLength = 0;                 // trainer: max length of abbreviations generated (0 = unlimited) 0, 2 - 6
  uint8_t wordLength = 0;                   // trainer: max length of english words generated (0 = unlimited) 0, 2 - 6
  uint8_t trainerDisplay = 1; // trainer: how we display what the trainer generates: nothing, by character, or by word  0 - 2
  uint8_t curtisBTiming = 45;               // keyer: timing for enhanced Curtis mode: dah                    0 - 10
  uint8_t curtisBDotTimng = 75 ;           // keyer: timing for enhanced Curtis mode: dit                    0 - 100
  //uint8_t interWdSpace = 7;               // trainer: normal interword spacing in lengths of dit,           6 - 45 ; default = norm = 7
  int interWdSpace = 7;               // trainer: normal interword spacing in lengths of dit,           6 - 45 ; default = norm = 7

  uint8_t echoRepeats = 3;                  // how often will echo trainer repeat an erroniously entered word? 0 - 7, 7=forever, default = 3
  uint8_t echoDisplay = 3;                  //  1 = CODE_ONLY 2 = DISP_ONLY 3 = CODE_AND_DISP
  //boolean wordDoubler = false;              // in CW trainer mode only, repeat each word
  uint8_t wordDoubler = 0;  // 0 - Off, 1 - On, 2 - On (less ICS), 3 - On (true WPM)
  uint8_t echoToneShift = 1;                // 0 = no shift, 1 = up, 2 = down (a half tone)                   0 - 2
  boolean echoConf = true;                  // true if echo trainer confirms audibly too, not just visually
//  uint8_t keyTrainerMode = 1;               // key a transmitter in generator and player mode?
                                              //  0: "Never";  1: "CW Keyer only";  2: "Keyer&Generator"; 3: Keyer, generator and LoRa / Internet RX
//  uint8_t loraTrainerMode = 0;              // transmit via LoRa or WiFi in generator and player mode?
                                              //  0: "No";  1: "LoRa" 2: "WiFi"
//  uint8_t goertzelBandwidth = 0;            //  0: "Wide" 1: "Narrow" 
  boolean speedAdapt = false;               //  true: in echo mode, increase speed when OK, reduce when not ok     
  uint8_t latency = 5;                      //  time span after currently sent element during which paddles are not checked; in 1/8th of dit length; stored as 1 -  8  
  uint8_t randomFile = 0;                   // if 0, play file word by word; if 255, skip random number of words (0 - 255) between reads   

  uint8_t kochOrder = 0;  // 0 - KOCH, 1 - LCWO, 2 - CWAC, 3 - LICW
  uint8_t kochLesson = 1;  // there are 51 chars so 51 lessons
  uint8_t lcwoLesson = 1;  // there are 51 chars so 51 lessons
  uint8_t cwacLesson = 1;  // there are 51 chars so 51 lessons
  uint8_t licwLesson = 1;  // there are 51 chars so 51 lessons
  uint8_t kochSingle = 0;  // 0 - all chars up to lesson number, 1 - only lesson char
  boolean kochShowMorse = true;  //add to NVS // 0 - off, 1 - on 
  uint8_t kochCWTimer = 1;  // 1 - 9 => 10 - 90 seconds
//  uint8_t kochFilter = 5;                   // constrain output to characters learned according to Koch's method 2 - 50 (45??)
//  boolean lcwoKochSeq = false;              // if true, replace native sequence with LCWO sequence 
//  boolean cwacKochSeq = false;              // if true, replace native sequence with CWops CW Academy sequence 
//  boolean licwKochSeq = false;              // if true, replace native sequence with LICW Carousel sequence 
//  uint8_t carouselStart = 0;                // Offset for LICW Carousel
//  uint8_t kochCharsLength = 52;             // # of chars for Koch training
//  boolean useCustomCharSet = false;         // if true, use Koch trainer with custom character set (imported from file)  
//  String  customCharSet = "";               // a place to store the custom character set

//  boolean extAudioOnDecode = false;         // send decoded audio also to external audio  I/O port
//  uint8_t timeOut = 1;                      // time-out value: 4 = no timeout, 1 = 5 min, 2 = 10 min, 3 = 15 min
//  boolean quickStart = false;               // should we start the last executed command immediately?
  boolean autoStopMode = true;                 // If to stop after each word in generator modes
//  uint8_t loraSyncW = 0x27;                 // allows to set different LoRa sync words, and so creating virtual "channels"
  uint8_t maxSequence = 0;                  // max # of words generated before the Morserino pauses

//  uint8_t serialOut = 5;                    // shall we output characters on USB serial? 
  uint8_t responsePause = 5;                // in echoTrainer mode, how long do we wait for response? in interWordSpaces; 2-12, default 5
  uint8_t wpm = 15;                         // keyer speed in words per minute                  5 - 60
  uint8_t promptPause = 2;                 // in echoTrainer mode, length of pause before we send next word; multiplied by interWordSpace
  uint8_t wordPause = 0;                  // in cwgenerator, length of pause before we send next word in seconds

  ///// stored in preferences, but not adjustable through preferences menu:
//  uint8_t menuPtr = 1;                      // current position of menu
//  String  wlanSSID = "";                    // SSID for connecting to the Internet
//  String  wlanPassword = "";                // password for connecting to WiFi router
//  String  wlanTRXPeer = "";                 // peer Morserino for WiFI TRX
//  String  wlanSSID1 = "";                    // SSID for connecting to the Internet
//  String  wlanPassword1 = "";                // password for connecting to WiFi router
//  String  wlanTRXPeer1 = "";                 // peer Morserino for WiFI TRX
//  String  wlanSSID2 = "";                    // SSID for connecting to the Internet
//  String  wlanPassword2 = "";                // password for connecting to WiFi router
//  String  wlanTRXPeer2 = "";                 // peer Morserino for WiFI TRX
//  String  wlanSSID3 = "";                    // SSID for connecting to the Internet
//  String  wlanPassword3 = "";                // password for connecting to WiFi router
//  String  wlanTRXPeer3 = "";                 // peer Morserino for WiFI TRX

  uint8_t saveGenMode = 0;  // save selected generatorMode            
  uint32_t fileWdPointer1 = 0;  // where in RealQSOs file         
  uint32_t fileWdPointer2 = 0;  // where in 2 word phrase file         
  uint32_t fileWdPointer3 = 0;  // where in 3 word phrase file         
  uint32_t fileWdPointer4 = 0;  // where in 4 owrd phrase file         
//  uint8_t tLeft = 20;                       // threshold for left paddle
//  uint8_t tRight = 20;                      // threshold for right paddle

//  uint8_t vAdjust = 180;                    // correction value: 155 - 250
  
//  uint8_t loraBand = 0;                     // 0 = 433, 1 = 868, 2 = 920

//  uint32_t loraQRG = QRG433;                // for 70 cm band

//  uint8_t snapShots = 0;                    // keep track which snapshots are being used ( 0 .. 7, called 1 to 8)

//  uint8_t boardVersion = 0;                 // which Morserino board version? v3 uses heltec Wifi Lora V2, V4 uses V2.1

//  uint8_t oledBrightness = 255;

/******************************************************/



/******************************************************/

#define SizeOfArray(x)       (sizeof(x) / sizeof(x[0]))

// enums for preferences

enum prefAllNames { prefKeyerMode, prefWPM, prefPitch, prefVolume, prefLatency, 
    prefPolarity, prefCurtisBDotT, prefCurtisBDashT, prefAutoCharSpace,
    prefRandomOption,
    prefRandomLength,
    prefAbbrWdLength,
    prefMaxSequence, 
    prefCWGenDisplay,
    prefRandomFile,
    prefInterCharSpace,
    prefInterWordSpace,
    prefStopNextRep, prefWordDoubler,
    prefEchoDisplay,
    prefWordPause,
    prefToneShift,
    prefEchoRepeats, 
    prefPromptPause, 
    prefResponsePause,
    prefEchoConf,
    prefSpeedAdapt,
    prefShowScrollHelp,
    prefKochOrder,
    prefKochLesson,
    prefKochSingle,
    prefKochShowMorse,
    prefKochCWTimer
    };
    
prefAllNames prefCWKEYER[] = { prefKeyerMode, prefWPM, prefPitch, prefVolume,
    prefPolarity, prefLatency, prefCurtisBDotT, prefCurtisBDashT,
    prefAutoCharSpace };

prefAllNames prefCWGENERATOR[] = { 
    prefWPM,
    prefPitch, 
    prefVolume,
    prefStopNextRep, 
    prefMaxSequence, 
    prefWordDoubler,
    prefRandomOption,
    prefRandomLength,
    prefAbbrWdLength,
    prefCWGenDisplay,
    prefRandomFile,
    prefWordPause,
    prefToneShift,
    prefInterCharSpace,
    prefInterWordSpace
    };
    
prefAllNames prefECHOTRAINER[] = { 
    prefWPM,
    prefPitch, 
    prefVolume,
    prefToneShift,
    prefEchoDisplay, 
    prefEchoRepeats, 
    prefPromptPause, 
    prefResponsePause,
    prefEchoConf,
    prefSpeedAdapt
    };
    
prefAllNames prefKOCHTRAINER[] = { 
    prefKochOrder,
    prefKochSingle,
    prefKochShowMorse,
    prefWPM,
    prefPitch, 
    prefVolume,
    prefKochCWTimer,
    prefToneShift,
    prefEchoDisplay, 
    prefEchoRepeats, 
    prefPromptPause, 
    prefResponsePause,
    prefEchoConf,
    prefSpeedAdapt,
    prefKochLesson
    };
    
prefAllNames prefSETTINGS[] = { prefKeyerMode, prefWPM, prefPitch, prefVolume, prefLatency, 
    prefPolarity, prefCurtisBDotT, prefCurtisBDashT, prefAutoCharSpace,
    prefRandomOption,
    prefRandomLength,
    prefAbbrWdLength,
    prefMaxSequence, 
    prefCWGenDisplay,
    prefRandomFile,
    prefInterCharSpace,
    prefInterWordSpace,
    prefStopNextRep, prefWordDoubler,
    prefEchoDisplay,
    prefWordPause,
    prefToneShift,
    prefEchoRepeats, 
    prefPromptPause, 
    prefResponsePause,
    prefEchoConf,
    prefSpeedAdapt,
    prefKochOrder,
    prefKochLesson,
    prefKochCWTimer,
    prefShowScrollHelp 
    };

// prefKeyerMode
String keyerModes[] = {"Curtis A", "Curtis B", "Ultimatic", "Non-Squeeze", "Straight Key"};
