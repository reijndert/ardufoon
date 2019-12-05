/*
 ardufoon
 created by R. de Haas - pd1rh - sept_2019
  
 hardware: RobotDyn D1-r2 and dfplayer mini
 compiled: set board to NodeMCU 1.0 12E
 pinout of RobotDyn D1-r2 WIFI: https://robotdyn.com/pub/media/0G-00005444==WIFI-D1R2-ESP8266/DOCS/PINOUT==0G-00005444==WIFI-D1R2-ESP8266.jpg
 pinout of DFPlayer: https://wiki.dfrobot.com/DFPlayer_Mini_SKU_DFR0299
 based on an idea found on https://apinventions.wordpress.com/wonderfoon/
 MP3 code : https://wiki.dfrobot.com/DFPlayer_Mini_SKU_DFR0299
 
 hardware:
 RobotDyn d1 r2 wifi and DFplayer MINI (mp3). --> install library DFRobotDFPlayermini first before compiling
 
 MP3 Player view from above:
 ----------+  +------------+
 01. VCC   |__|  16 busy   |
 02. rx          15 usb-   |
 03. tx          14 usb+   |
 04. dac_r       13 adkey_2|
 05. dac_i       12 adkey_1|
 06. spk_1       11 io_2   |
 07. gnd         10 gnd    |
 08. spk_2       09 io_1   |
 --------------------------+
*/

#include "DFRobotDFPlayerMini.h"
#include "SoftwareSerial.h"
#include "EEPROM.h"
#include "ESP8266WiFi.h"

#define DIALTONEOUT   13 // D7 425 Hz
#define HOOKIN        12 // D6 detect phone off-hook
#define DIALERBUSYIN   4 // D2 (yellow) dialer is being used
#define DIALERPULSESIN 5 // D1 (red)    dialer pulses appear here
                         //    (blue)   dialer, connect to groundpin on NodeMCU
#define MP3RX          0 // D3 softwareserial rx (0)
#define MP3TX          2 // D4      ''        tx (2)
#define FOLDERSELECT  14 // D5 pushbutton on phone (combination with dialer to change folder)

//states of FSM ( finite state machine )
#define S_IDLE              1
#define S_DIALTONE          2
#define S_WAITFORDIALERBUSY 3
#define S_WAITFORPULSES     4
#define S_PLAYMP3           5
#define S_VOLUMESELECT      6
#define S_FILENOTFOUND      7
#define S_PLAYFINISHED      8

#define HOOKISON       0
#define HOOKISOFF      1
#define DIALERBUSY     0
#define DIALERIDLE     1
#define PULSE          0
#define NOPULSE        1
#define BUTTONPUSHED   0
#define DEBOUNCEDELAY  4   // range : 3 - 9  , I choose 4 or 5 for my personal best results

SoftwareSerial mySoftwareSerial( MP3RX , MP3TX );  //rx, tx
DFRobotDFPlayerMini myDFPlayer;

int  sample             = 0;            // final value of puls counter sample
int  sample1            = 0;
int  sample2            = 0;
int  lastsample         = 0;            // to detect alternating sample values
int  count              = 0;            // number dialed
int  redialdelay        = 1000;         // when song starts, wait for mSec for next redial
char state              = S_IDLE;       // begin in this state
int  hookstatus         = HOOKISON;     // default horn is on phone
int  dialerstatus       = DIALERIDLE;   // start with status IDLE
int  playvolume         = 30;           // start with 30 = max volume
int  volumeincrement    = -5;
int  dialtonevolume     = 20;
int  foldertoplay       =  1;           // default play folder
int  folderselectstatus =  0;           // detect status of pushbutton
int  foldercounter      =  0;
int  filecount          =  0;
int  maxfolder          =  0;           // last folder with 10 songs
int  buttonpressed      =  0;
bool showedidle         =  true;
bool playfinished       =  false;
bool filenotfound       =  false;
bool cardonlineagain    =  false;
int  previouscount      =  0;
int  mp3currentstate    =  0;
int  timer1             =  0;           // poll timer for mp3 player
int  timer2             =  0;           // millisecs since last dial

uint addr = 0;
// fake data to start with
struct { 
  uint value1 = 0; //volume offset
  uint value2 = 1; //current mp3 folder
} data;

void setup() { //runs once

//hardware setup
pinMode(DIALTONEOUT   , OUTPUT);
pinMode(HOOKIN        , INPUT_PULLUP);
pinMode(DIALERBUSYIN  , INPUT_PULLUP);
pinMode(DIALERPULSESIN, INPUT_PULLUP);
pinMode(FOLDERSELECT  , INPUT_PULLUP);

//disable WIFI module of RobotDyn board, we dont need it and it saves power
WiFi.forceSleepBegin();

Serial.begin    (9600);             //for serial debugging
Serial.println  ("");
Serial.println  ("\n\nArdufoon Wonderfoon v 1.2 using RobotDyn D1-r2 WIFI");
Serial.println  ("DFPlayer MINI");
Serial.println  ("WIFI disabled to for less power consumption");
Serial.println  ("Created by Reijndert de Haas - PD1RH - Assen, The Netherlands");
Serial.println  ("E-Mail : apm.de.haas@gmail.com");
Serial.println  ("Compiled on December 3rd, 2019");

//control of MP3 Player
mySoftwareSerial.begin(9600);
Serial.println(F("Initializing DFPlayer ... (May sometimes take a few seconds)"));
if (!myDFPlayer.begin(mySoftwareSerial)) {  //Use softwareSerial to communicate with mp3.
    Serial.println(F("Unable to begin:"));
    Serial.println(F("1.Please recheck the connection!"));
    Serial.println(F("2.Please insert the SD card!")); 
    //hardware troubles
    Serial.println("\nFailed to init MP3 player, trying to restart");
    delay(5000);
    ESP.restart();           //reboot this thing. hope the mp3 connects now!
    while (true) {
      Serial.println("reboot failed");
      Serial.println("disconnect power and try again");
      delay(5000);
    }
  }
  else {
    Serial.println(F("DFPlayer Mini connected OK."));
  }
myDFPlayer.setTimeOut(800);                  //Set serial communictaion time out 500ms
myDFPlayer.volume    (30);                   //Set volume value. From 0 to 30
myDFPlayer.EQ        (DFPLAYER_EQ_NORMAL);   //Note: equaliser only usefull when using modern speaker
//myDFPlayer.enableLoop(); //enable loop.

EEPROM.begin(16);                                     //initialize virtual memory
Serial.print  ("Total mp3 files on disk: ");
Serial.println(myDFPlayer.readFileCounts());          //read all file counts in SD card
Serial.print  ("EEPROM contents: ");
EEPROM.get(addr,data);                                //first time use of uC ?
Serial.println("("+String(data.value1)+","+String(data.value2)+")");
if (data.value1>30||data.value2>10) {
    Serial.println("* No valid data found in EEPROM");
    Serial.println("* overwriting...");
    savesettings();
}

//display count of files in first 10 folders
foldercounter=1;
while(foldercounter<11){
    Serial.print  ("Files in folder ");
    if (foldercounter<10) {
        Serial.print("0");                            //for pretty debugging :)
    }
    Serial.print  (foldercounter);
    Serial.print  (": ");
    filecount = myDFPlayer.readFileCountsInFolder(foldercounter);
    Serial.println(filecount);
    if (filecount==10) {
      maxfolder = foldercounter;
    }
    foldercounter++;
}
Serial.print  ("Highest folder with 10 songs in it: ");
Serial.println(maxfolder);
Serial.println("Notice: special sounds like dialtone are in folder 99");
Serial.println("=====================================================");
Serial.print  ("Number of files in folder 99 : ");
Serial.println(myDFPlayer.readFileCountsInFolder(99));
readsettings();
showedidle=false;
}


//-----------------------------------------------------------------------

void loop() {  //main loop of the program. the state machine is in here
delay(250);    //poll delay in millisec

if (millis() - timer1 > 10000) { // printDetail is more responsive this way
                                // dont ask me why ;-)
    timer1 = millis();
    mp3currentstate = myDFPlayer.readState();
    //Serial.print  ("MP3 state : ");
    //Serial.println(mp3currentstate);
   
   }
   
if (myDFPlayer.available()) {
    printDetail(myDFPlayer.readType(), myDFPlayer.read()); //Print the detail message from DFPlayer to handle different errors and states.
   }

hookstatus          = digitalRead(HOOKIN);
dialerstatus        = digitalRead(DIALERBUSYIN);
folderselectstatus  = digitalRead(FOLDERSELECT);
buttonpressed       = folderselectstatus;

if (cardonlineagain) {
  Serial.println("MP3 player recovering...");
  cardonlineagain = false;
  state = S_DIALTONE;
  
}

if (hookstatus == HOOKISON){
  state = S_IDLE;
}

//this trick often doesnt work...
if (playfinished) {
  state = S_IDLE;
}

if (filenotfound) {
  state = S_FILENOTFOUND;
}

if (playfinished) {
  state = S_PLAYFINISHED;
}

if ((buttonpressed==BUTTONPUSHED) && (state==S_WAITFORDIALERBUSY)) {
  delay(500); //still pushed?
  if (digitalRead(FOLDERSELECT)==BUTTONPUSHED) {
    state = S_VOLUMESELECT;
  }
}

/*
//testroutine 1
if (false) { //true or false
    myDFPlayer.volume (20);
    Serial.println("Testing 1 2 3 ... Play song 2 in folder 1");
    myDFPlayer.playFolder(1, 2);
    while (true) { // infinite loop
      delay(5000);
    }
}
//end of testroutine 1

//testroutine 2
if (false) { //true or false
    myDFPlayer.volume (20);
    Serial.println("Play song 3 in folder 2");
    myDFPlayer.playFolder(2, 3);
    myDFPlayer.enableLoop();
    while (true) {
      hookstatus          = digitalRead(HOOKIN);
      dialerstatus        = digitalRead(DIALERBUSYIN);
      folderselectstatus  = digitalRead(FOLDERSELECT);
      Serial.print  (hookstatus);
      Serial.print  (dialerstatus);
      Serial.println(folderselectstatus);      
      delay(500);
    }
}
//end of testroutine 2
*/

//ardufoon state-machine
switch (state) {
    case S_IDLE:            //phone is idle, waiting for 'pick up phone' signal
    if (!showedidle) {      //no need for flooding debug monitor :)
      Serial.println("\nPhone idle");
      showedidle    = true;
      playfinished  = false;
    }

    if (hookstatus == HOOKISOFF) {
        state = S_DIALTONE;
        Serial.println("Handset detected");        
        showedidle = false;
    }
    else { myDFPlayer.stop();   //mp3 pause; silent
    }
    break;

    case S_DIALTONE: //play dialtone mp3
    Serial.println  ("Play dialtone");
    myDFPlayer.volume (dialtonevolume);
    delay(1000);
    myDFPlayer.playFolder(99, 99); // dialtone
    state = S_WAITFORDIALERBUSY;
    break;

    case S_WAITFORDIALERBUSY:
    if ( dialerstatus == DIALERBUSY){
      Serial.println("Rotary disk is busy now");      
      myDFPlayer.pause();  //mp3 silent; pause
      state = S_WAITFORPULSES;
    }
    break;

    case S_WAITFORPULSES:
    Serial.println("Waiting for dial-pulses");
    previouscount = count;
    count = 0;
    do {
      lastsample = sample;
      sample1 = digitalRead(DIALERPULSESIN);
      delay( DEBOUNCEDELAY );
      sample2 = digitalRead(DIALERPULSESIN);
      if ( sample1 == sample2 ){ //assume valid pulse detected when both samples are the same
           sample = sample2;
           }
        
          if ( lastsample != sample) {  //detect high/low low/high changes
            if (sample2 == PULSE) {
                count++;
            }
            Serial.print("o");            
            lastsample = sample;
          }
    }
    while (digitalRead(DIALERBUSYIN) == DIALERBUSY);
    //rotary disk is in resting position now

    if (digitalRead(FOLDERSELECT)==BUTTONPUSHED) {
        Serial.println("\nPushbutton pressed");
        Serial.print("Folder changed to " );        
        Serial.println(count);   
        foldertoplay = count;
        if (myDFPlayer.readFileCountsInFolder(foldertoplay)<1) {
          Serial.println("No files in folder, defaulting to folder 1");
          foldertoplay = 1;
          delay(500);
          myDFPlayer.playFolder(99,97);
          delay(1500);
          myDFPlayer.pause();
        }
        savesettings();
        myDFPlayer.volume(dialtonevolume);
        myDFPlayer.playFolder(99, 98); //beep beep
        //myDFPlayer.disableLoop();

        delay(3000);        
        myDFPlayer.pause();
        state = S_IDLE;
        }
    else {
        state = S_PLAYMP3;
        Serial.print  ("\nSong selected = ");
        Serial.println(count);   
    }
  //quick redial ?
    Serial.print  ("Time since last dial: ");
    Serial.println(millis() - timer2);
    if (millis() - timer2 < 4000 ) { //fast redial
        foldertoplay = previouscount; 
        Serial.println("* New folder");  
        if (foldertoplay>maxfolder) {
            Serial.println("* folder out of bound, resetting to 1");
            foldertoplay=1;
        }
        savesettings();
    }
    timer2 = millis();
    //Serial.print("Previouscount: "); Serial.println(previouscount);
    //Serial.print("Currentcount : "); Serial.println(count);
    break; 

    case S_FILENOTFOUND:
    Serial.println("Play three tone sound");
    //delay(500);
    //myDFPlayer.volume(dialtonevolume);
    //delay(500);
    myDFPlayer.playFolder(99,97); //three tones
    delay(3000);
    myDFPlayer.pause();
    filenotfound=false;
    state = S_DIALTONE;
    break;

    case S_PLAYFINISHED:
    Serial.println("Play finished");
    myDFPlayer.volume(dialtonevolume);
    myDFPlayer.playFolder(99,98); //beep beep 'no connection'
    state = S_WAITFORDIALERBUSY;
    delay(500);
    playfinished=false;
    break;

    case S_VOLUMESELECT:
    playvolume += volumeincrement;
    Serial.print("* v:"); Serial.println    ( playvolume );
    myDFPlayer.volume ( playvolume );
    if(playvolume<=15||playvolume>=30) { 
      volumeincrement = -volumeincrement;
      Serial.println("Changed volume up/down direction ");
    }
    state = S_WAITFORDIALERBUSY;
    //savesettings();
    break;

    case S_PLAYMP3:
    readsettings();
    myDFPlayer.volume(playvolume);
    if (count<1||count>10) {  // check on irratic values
      Serial.println("Sequence value corrected to 1");
      count=1;
    }
        
    Serial.print   ( "p");  Serial.print  (count);
    Serial.print   ("-f"); Serial.print  (foldertoplay);
    Serial.print   ("-v"); Serial.println(playvolume);
    myDFPlayer.playFolder(foldertoplay,count);    //Play the selected mp3 of folder
/*    
   if (previouscount==count){
      myDFPlayer.enableLoop();
      Serial.println("Loop playing enabled");
    }
    else {
      myDFPlayer.disableLoop();
      Serial.println("Normal play, no loop");
    }
*/    
    //Serial.println("Delaying dialer");
    //delay( redialdelay);  // wait before allowing new redial
    Serial.println("Ready for input");
    state = S_WAITFORDIALERBUSY;
    break;
  }
}

void readsettings() {
    EEPROM.get(addr,data);
    //playvolume = data.value1;                   //get volumeoffset from EEPROM
    foldertoplay = data.value2;                   //get folder from EEPROM
    if (foldertoplay<1||foldertoplay>10) {
      Serial.println("Foldertoplay value corrected to 1");
      foldertoplay=1;
    }
    if (playvolume<1||playvolume>30) {
      Serial.println("Volumeoffset value corrected to 30");
      playvolume=30;
    }
    
    Serial.println("* settings read");
}

void savesettings() {
    data.value1 = playvolume;                   //save volume
    data.value2 = foldertoplay;                 //save playfolder
    EEPROM.put(addr,data);
    EEPROM.commit();
    Serial.println("* settings saved");
}

//mp3 error handling
void printDetail(uint8_t type, int value){
  switch (type) {
    case TimeOut:
      //Serial.println(F("Time Out!"));
      break;
    case WrongStack:
      Serial.println(F("Stack Wrong!"));
      playfinished = true;
      break;
    case DFPlayerCardInserted:
      Serial.println(F("Card Inserted!"));
      break;
    case DFPlayerCardRemoved:
      Serial.println(F("Card Removed!"));
      break;
    case DFPlayerCardOnline:
      Serial.println(F("Card Online!"));
      cardonlineagain = true;
      break;
    case DFPlayerPlayFinished:
      Serial.print(F("Number:"));
      Serial.print(value);
      Serial.println(F(" Play Finished!"));  // unreliable readings...
      playfinished = true;
      break;
    case DFPlayerError:
      Serial.print(F("DFPlayerError:"));
      switch (value) {
        case Busy:
          Serial.println(F("Card not found"));
          break;
        case Sleeping:
          Serial.println(F("Sleeping"));
          break;
        case SerialWrongStack:
          Serial.println(F("Get Wrong Stack"));
          playfinished = true;
          break;
        case CheckSumNotMatch:
          Serial.println(F("Check Sum Not Match"));
          playfinished = true;
          break;
        case FileIndexOut:
          Serial.println(F("File Index Out of Bound"));
          break;
        case FileMismatch:
          Serial.println(F("Cannot Find File"));
          filenotfound=true;
          break;
        case Advertise:
          Serial.println(F("In Advertise"));
          break;
        default:
          break;
      }
      break;
    default:
      break;
  }
}
