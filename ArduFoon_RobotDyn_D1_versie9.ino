/*
 ardufoon, source available on Github
 
 created by R. de Haas - pd1rh -  sept_2019
                                  jun__2020
  
 hardware: RobotDyn D1-r2 , dfplayer mini and H-Bridge for ringer control
 compiled: set board to NodeMCU 1.0 12E
 pinout of RobotDyn D1-r2 WIFI:  https://robotdyn.com/pub/media/0G-00005444==WIFI-D1R2-ESP8266/DOCS/PINOUT==0G-00005444==WIFI-D1R2-ESP8266.jpg
 pinout of DFPlayer:             https://wiki.dfrobot.com/DFPlayer_Mini_SKU_DFR0299
 based on an idea found on       https://apinventions.wordpress.com/wonderfoon/
 MP3 code :                      https://wiki.dfrobot.com/DFPlayer_Mini_SKU_DFR0299
 
 hardware:
 RobotDyn d1 r2 wifi and DFplayer MINI (mp3),
 install library DFRobotDFPlayermini first before compiling
 
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
#define   ARDUFOONVERSION "Ardufoon v1.9 9JUN2020 R. de Haas - pd1rh"


// port definitions
#define RINGERBELL1    16 // D0 first  controlpin for H-Bridge ringer circuit
#define RINGERBELL2    13 // D7 second controlpin for H-Bridge ringer circuit
                          //    also used for LED, so make a choice ;-)
                          //    Variable "use_bell" decides what to do
                          
#define DIALERPULSESIN  5 // D1 (red)    dialer pulses appear here
                          //    (blue)   dialer connect to command ground
#define DIALERBUSYIN    4 // D2 (yellow) dialer is being used
#define MP3RX           0 // D3 softwareserial rx (0)
#define MP3TX           2 // D4      ''        tx (2)
#define FOLDERSELECT   14 // D5 pushbutton on phone (combination with dialer to change folder)
#define HOOKIN         12 // D6 detect phone off-hook
#define ROTARYLED      16 // D0 or D7 statusled , flashes rotary disk diagnostics
                          // D0 is blue onboard LED of NodeMCU
                          //    Use *only* when NO bell attached

//#define in1 55           // d?  t.b.d. hammer control for ringer
//#define in2 44           // d?  t.b.d. "  ( please use inverter to save i/o port
                         
//states of FSM ( finite state machine )
#define S_IDLE              1
#define S_DIALTONE          2
#define S_WAITFORDIALERBUSY 3
#define S_WAITFORPULSES     4
#define S_PLAYMP3           5
#define S_VOLUMESELECT      6
#define S_FILENOTFOUND      7
#define S_PLAYFINISHED      8
#define S_PANIC             9
#define S_PLAYRINGTONE     10
#define S_PLAYRANDOMLY     11

//status flags
#define HOOKISON       0
#define HOOKISOFF      1
#define DIALERBUSY     0
#define DIALERIDLE     1
#define PULSE          0
#define NOPULSE        1
#define BUTTONPUSHED   0

//constant
#define DEBOUNCEDELAY  4         // suggested range : 3 - 9, it depends on the quality of the dialdisk. 4 is safe choice i think.
#define TESTMODE       false

SoftwareSerial mySoftwareSerial( MP3RX , MP3TX );  //rx, tx
DFRobotDFPlayerMini myDFPlayer;

char state              = S_IDLE;       // begin in this state
int  sample             = 0;            // final value of puls counter sample
int  sample1            = 0;            // debounce temp variable
int  sample2            = 0;            // debounce temp variable
int  lastsample         = 0;            // to detect alternating sample values
int  count              = 0;            // number dialed
int  hookstatus         = HOOKISON;     // default horn is on phone
int  dialerstatus       = DIALERIDLE;   // start with status IDLE
int  playvolume         = 28;           
int  volumeincrement    = +4;
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
int  timer3             =  0;           // for use with dialstring evaluation
String dialstring       = "";           // reversed dialstring
int  dialtoneplaying    =  0;           // used by volume button
int  folderislocked     =  0;           // status lock to a playfolder
int  lockedfolder       =  1;           // which folder is the locked folder
int  use_bell           =  0;
int  soscount           =  0;
int  randomfolder       =  0;
int  randomsong         =  0;
bool randomenabled      =  false;
int  lastrandomplayed   =  0;

// non volatile memory structure
uint addr = 0;
struct {                //defaults
  uint value1 = 24;     //volume 
  uint value2 =  1;     //current mp3 folder
  uint value3 =  0;     //folder is locked ( status flag )
  uint value4 =  1;     //locked folder ( always play this folder )
  uint value5 =  false; //random enabled status
} data;


void setup() { //runs once at startup
  
pinMode(RINGERBELL1   , OUTPUT);
pinMode(RINGERBELL2   , OUTPUT);
pinMode(ROTARYLED     , OUTPUT);
pinMode(HOOKIN        , INPUT_PULLUP);
pinMode(DIALERBUSYIN  , INPUT_PULLUP);
pinMode(DIALERPULSESIN, INPUT_PULLUP);
pinMode(FOLDERSELECT  , INPUT_PULLUP);

//fastblink(1,1000);        //show robotdyn is alive !
//delay(500);

digitalWrite(RINGERBELL1, HIGH);    //onboard LED off ( NodeMCU )
WiFi.forceSleepBegin();   //disable WIFI module of RobotDyn board, we dont need it and it saves some milli-amps

Serial.begin    (9600);             //for serial debugging
Serial.println  ("");
Serial.print    ("\n\nArdufoon Wonderfoon ");
Serial.print    (ARDUFOONVERSION);
Serial.println  ("using RobotDyn D1-r2 WIFI and DFPlayer MINI");
Serial.println  ("Created by Reijndert de Haas - PD1RH - Assen, The Netherlands");
Serial.println  ("E-Mail : apm.de.haas@gmail.com");


Serial.println("Check if we are in testmode...");
if ( TESTMODE ) {
  Serial.println("Yes, we are");
}
else {
  Serial.println("Nope");
}

//testroutine
while (TESTMODE) {
      hookstatus          = digitalRead(HOOKIN);
      dialerstatus        = digitalRead(DIALERBUSYIN);
      folderselectstatus  = digitalRead(FOLDERSELECT);
      Serial.print  (hookstatus);
      Serial.print  (dialerstatus);
      Serial.println(folderselectstatus);      
      randomfolder = random(10)+1;
      randomsong   = random(10)+1 ;
      Serial.print("Seed:");  Serial.println(analogRead(0)); // this pin needs an antenna :-)
      Serial.print("F:");     Serial.println (randomfolder);
      Serial.print("S:");     Serial.println (randomsong  );
      Serial.println("");
      delay(2000);    
}
//end of testroutine

Serial.println("Not in testmode, continuing.");

//control of MP3 Player
mySoftwareSerial.begin(9600);
Serial.println(F("Initializing DFPlayer ... (May sometimes take a few seconds)"));
if (!myDFPlayer.begin(mySoftwareSerial)) {  //Use softwareSerial to communicate with mp3.
    Serial.println(F("Unable to begin:"));
    Serial.println(F("1.Please recheck the connection!"));
    Serial.println(F("2.Please insert the SD card!")); 
    //hardware troubles
    Serial.println("\nFailed to init MP3 player, trying to restart");
    soscount = 1;
    sos();
    ESP.restart();           //reboot this thing. hope the mp3 connects now!
    while (true) {
      Serial.println("reboot failed");
      Serial.println("disconnect power and try again");      
      sos();
    }
  }
  else {
    Serial.println(F("DFPlayer Mini connected OK."));
  }

fastblink(1,1000);                           //Initialize succeeded

myDFPlayer.setTimeOut(800);                  //Set serial communictaion time out 500ms
myDFPlayer.volume    (playvolume);           //Set volume value. From 0 to 30
myDFPlayer.EQ        (DFPLAYER_EQ_NORMAL);   //Note: equaliser only usefull when using modern speaker

EEPROM.begin(64);                                     //initialize virtual memory
readsettings();
Serial.println("EEPROM contents");
Serial.print  ("Last Volume: "); Serial.println(playvolume);
Serial.print  ("Last Folder: "); Serial.println(foldertoplay);
Serial.print  ("Use locking: "); Serial.println(folderislocked);
Serial.print  ("Lock to    : "); Serial.println(lockedfolder);
Serial.print  ("Random     : "); Serial.println(randomenabled);

readsettings();
foldercounter=1;
while(foldercounter<11){                              //check first 10 folders
    Serial.print  ("Files in folder ");
    if (foldercounter<10) {
        Serial.print("0");                            //for pretty debugging :)
    }
    Serial.print  (foldercounter);
    Serial.print  (": ");
    filecount = myDFPlayer.readFileCountsInFolder(foldercounter);
    Serial.print(filecount);
    if (filecount>=10) {
      maxfolder = foldercounter;
      Serial.println("");
    }
    else {
      Serial.println(" *exit*");
      break; // quit on first folder with filecount < 10
    }
    foldercounter++;
    fastblink(1,50);
}
Serial.print  ("Maxfolder now: ");
Serial.println(maxfolder);

timer3            = millis();
randomSeed( timer3 ); // initialise randomizer seed
Serial.print( "Random seed: "); Serial.println(timer3);

if ( !myDFPlayer.readFileCountsInFolder(99) == 4) {
  state=S_PANIC;
}

readsettings();
foldertoplay      = 1;          // choose folder 1 after a reboot
showedidle        = false;
dialstring        = "";
}


//-----------------------------------------------------------------------

void loop() {  //main loop of the program. the state machine is in here
delay(50);     //poll delay in millisec

if (millis() - timer1 > 5000) { // readstate every 10 secs.
                                // printDetail is more responsive this way
    timer1 = millis();
    mp3currentstate = myDFPlayer.readState();  //also checks "song ended"
   }
   
//should not be sending this under normal condition since it was available already...
if (myDFPlayer.available()) {
    printDetail(myDFPlayer.readType(), myDFPlayer.read()); //Print the detail message from DFPlayer to handle different errors and states.
   }

hookstatus          = digitalRead(HOOKIN);
dialerstatus        = digitalRead(DIALERBUSYIN);
folderselectstatus  = digitalRead(FOLDERSELECT);
buttonpressed       = folderselectstatus;

//should not happen at any time under normal conditions
if (cardonlineagain) {
  Serial.println("MP3 player recovering...");
  cardonlineagain = false;
  state = S_DIALTONE; 
}

if (hookstatus == HOOKISON){
  state = S_IDLE;
}

if (filenotfound) {
  state = S_FILENOTFOUND;
}

if (playfinished) {   // depends on mp3 reporting, not always working :(
  state = S_PLAYFINISHED;
}

if ((buttonpressed==BUTTONPUSHED) && (state==S_WAITFORDIALERBUSY)) {
  delay(200);
  if (digitalRead(FOLDERSELECT)==BUTTONPUSHED) {  //still pushed?
    state = S_VOLUMESELECT;
  }
}

//ardufoon state-machine
switch (state) {
    case S_IDLE:            //phone is idle, waiting for 'pick up phone' signal
    
    if (!showedidle) {      //no need for flooding debug monitor :)
      Serial.println("Phone idle");
      showedidle    = true;
      playfinished  = false;
//      fastblink(1,50);
    }

    if (hookstatus == HOOKISOFF) {
        Serial.println("Phone is off-hook now");        
        showedidle = false;
        fastblink(1,50);        
        state = S_DIALTONE;
    }
    else { myDFPlayer.stop(); }   //mp3 silent    
    break;

    case S_DIALTONE: //play dialtone mp3
    Serial.println  ("Play dialtone");
    myDFPlayer.volume (dialtonevolume);
    myDFPlayer.playFolder(99, 99); // dialtone
    delay(200);
    dialstring="";                 // clear dialstring
    dialtoneplaying = 1;
    state = S_WAITFORDIALERBUSY;
    if (randomenabled) {           // previous song ended in random-mode, continue playing randomly
      state = S_PLAYRANDOMLY;
      Serial.println("Continue randomplay unattended");   
    } 
    break;

    case S_WAITFORDIALERBUSY:
    if ( dialerstatus == DIALERBUSY){
      Serial.println("Rotary disk is busy now");      
      myDFPlayer.stop();        //mp3 silent
      if (randomenabled) {
        Serial.println("Random play disabled");
      }
      randomenabled = false;    //disable random functions      
      savesettings();
      fastblink(1,50);
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

    if (digitalRead(FOLDERSELECT)==BUTTONPUSHED) {  //lock to this folder
        folderislocked = 1;
        Serial.println();
        Serial.println(ARDUFOONVERSION);
        Serial.print  ("Info: hightest folder is "); Serial.println(maxfolder);
        dialstring="";
        Serial.print("Folder changed to " ); Serial.println(count);   
        Serial.print("Pushbutton pressed, locking this folder "); Serial.println(count);
        fastblink(1,500);
        if (myDFPlayer.readFileCountsInFolder(count)<1) { //@@
          Serial.println("No files in folder, defaulting to folder 1");
          foldertoplay = 1;
          folderislocked = 0;              // cancel the option
          myDFPlayer.playFolder(99,97);
          fastblink(4,300);
          myDFPlayer.stop();
        }
        foldertoplay = count;
        lockedfolder = foldertoplay;
        savesettings();
        if ( folderislocked == 1 ) {        // always check, it may be cancelled ;-)
             myDFPlayer.playFolder(99, 96); // confirmation message "draai opnieuw"
             randomsong = 0;                // also reset randomsong in case of incremental play
             delay(3000);
             }
        myDFPlayer.stop();
        state = S_IDLE;
        }
    else {
        state = S_PLAYMP3;        
        dialstring=char(count+48 ) + dialstring;
        Serial.println("");
        if (millis() - timer3 > 8000) { // dialstring check, add or initialise
          timer3=millis();
          Serial.println("Slow dial");
          dialstring = char(count+48);
        }
        
        if (dialstring.length()>3){                // max 3 characters please :)
            dialstring=dialstring.substring(0,3);
        }
        Serial.print  ("Song selected = ");
        Serial.println(count);           
    }
  //quick redial ?
    Serial.print  ("Time since last dial: ");
    Serial.println(millis() - timer2);
    if (millis() - timer2 < 4000 ) {            //fast redial
        foldertoplay = previouscount; 
        Serial.print  ("* New folder: ");  
        Serial.println(foldertoplay);
        if (foldertoplay>maxfolder) {
            Serial.print  ("* max folder is: "); Serial.println(maxfolder);
            Serial.println("* folder out of bounds, resetting to 1");
            foldertoplay=1;
        }
        savesettings();
    }
    timer2 = millis();
    randomSeed( timer2 ); // (re-)initialise randomizer seed
    Serial.println("New random seed");
    break; 

    case S_FILENOTFOUND:
    Serial.println("Play sound *unknown*");
    myDFPlayer.playFolder(99,97); //three tones
    sos();
    //delay(1800);
    myDFPlayer.stop();
    filenotfound=false;
    state = S_DIALTONE;
    break;

    case S_PLAYFINISHED:
    Serial.println("Play finished");
    myDFPlayer.volume(dialtonevolume);
    myDFPlayer.playFolder(99,98); //long beeps 'no connection'
    state = S_WAITFORDIALERBUSY;
    playfinished=false;
    if (randomenabled) {           // previous song ended in random-mode, continue randomly
      state = S_PLAYRANDOMLY;
      Serial.println("Continue randomplay unattended");
    }
    break;

    case S_VOLUMESELECT:
    if (dialtoneplaying == 1 || randomenabled ) {        
        Serial.print("Random mode, ");
        fastblink(1,200);
        randomenabled = true;
        savesettings();
        state = S_PLAYRANDOMLY;        
        break;
    }
    playvolume += volumeincrement;
    Serial.print("* v:"); Serial.println    ( playvolume );
    myDFPlayer.volume ( playvolume );
    if(playvolume<=15||playvolume>=30) { 
      volumeincrement = -volumeincrement;
      Serial.println("Changed volume up/down direction ");
    }
    state = S_WAITFORDIALERBUSY;
    savesettings();
    delay(200);
    break;

    case S_PLAYMP3:
    randomSeed( millis() ); // (re-)initialise randomizer seed
    Serial.print("Dialstring (reversed) is now: "); Serial.println(dialstring);
    //readsettings();
    dialtoneplaying = 0;
    myDFPlayer.volume(playvolume);
    if (count<1||count>10) {  // check on irratic values
      Serial.println("Sequence value corrected to 1");
      count=1;
    }
    if (foldertoplay>maxfolder) {
      Serial.print("Folder reset from "); Serial.print(foldertoplay); Serial.println(" to 1");
        foldertoplay=1;
        savesettings();
    }
    if (folderislocked == 1) {
        Serial.print("Folder "); Serial.print(lockedfolder); Serial.println(" locking detected");    
        foldertoplay = lockedfolder;
        fastblink(2,100);
    }
    Serial.print   ( "Starting: p");  Serial.print  (count);
    Serial.print   ("-f"); Serial.print  (foldertoplay);
    Serial.print   ("-v"); Serial.println(playvolume);

    //dialed so far, check special numbers ( reverse ! )
    if (dialstring=="111") { 
      Serial.println(ARDUFOONVERSION);   
      Serial.print("Unlocking folder "); Serial.println(lockedfolder);    
      folderislocked = 0;
      dialstring="";                  //reset string buffer
      savesettings();
      myDFPlayer.volume(dialtonevolume);
      myDFPlayer.playFolder(99,95);   //confirmation message
      delay(4000);
      myDFPlayer.stop();
      state = S_IDLE;
      break;
    }
    else {
      if (dialstring=="2::") {      
      fastblink(1,200);
      Serial.println("002 message");
      myDFPlayer.playFolder(99,2);  //MP3 with fake time message
      dialstring="";                //reset string buffer
      }
      else {
        if (dialstring=="3::") {      
        fastblink(1,200);
        Serial.println("003 message");
        myDFPlayer.playFolder(99,3);  //MP3 with fake weather forecast
        dialstring="";                //reset string buffer
        }
        else {      
          if (dialstring=="8::") {
          fastblink(1,200); 
          Serial.println("008 message");     
          myDFPlayer.playFolder(99,8);  //MP3 with informational message
          dialstring="";                //reset string buffer
          }
          else {
          myDFPlayer.playFolder(foldertoplay,count);    //Play the selected mp3 of folder
          }
        }
      }
    }
    Serial.println("Ready for user input");
    state = S_WAITFORDIALERBUSY;
    break;

    case S_PLAYRANDOMLY:    
    randomfolder = random(maxfolder)+1;
    
    if (folderislocked == 1) {
        randomsong++;
        if (randomsong>10) {
          randomsong = 1;
        }
        Serial.print("Folder lock, ");// Serial.print(lockedfolder); Serial.println(" locking detected");    
        randomfolder = lockedfolder;
    }
    else {
        randomsong   = random(10)+1 ;  
    }
    myDFPlayer.volume ( playvolume );
    delay(100);
    Serial.print("F:") ; Serial.print   (randomfolder);
    Serial.print(" S:"); Serial.print   (randomsong  );
    Serial.print(" V:"); Serial.println (playvolume  );
    myDFPlayer.stop();
    delay(100);    
    myDFPlayer.playFolder(randomfolder,randomsong   );
    lastrandomplayed = randomfolder*10+randomsong ;
    dialtoneplaying  = 0;
    state = S_WAITFORDIALERBUSY;
    break;

    case S_PANIC:
    Serial.println  ("P A N I C");
    sos();
    break;

    case S_PLAYRINGTONE:
    Serial.print   ("Play ringtone");
    break;
  }
}

void fastblink(int blinkcount, int blinkdelay) {
  if (use_bell == 0) {
      for (int i = 0; i<blinkcount; i++ ) {
        digitalWrite(ROTARYLED,LOW);
        delay(blinkdelay / 2);
        digitalWrite(ROTARYLED,HIGH);
        delay(blinkdelay / 2);
        }
  }
  else {                           //fastblink is also used for delays, so generate one :)
    delay(blinkdelay);
  }
}

void sos() {                       //used for indicating a panic situation
    Serial.println("Sending an SOS");
    if ( soscount > 0 ) {               // use LED only when sos occurs more than once
      fastblink(3,200);    delay(800);  // 3 times dash
      fastblink(3,600);    delay(800);  // 3 times dot
      fastblink(3,200);    delay(2000); // 3 times dash then long delay
      soscount++;
    }
}

 void ring_bell(int duration) {
    for (int i=0;i<duration;i++) {
      // Set hammer to one side
      digitalWrite(RINGERBELL1, HIGH);
      digitalWrite(RINGERBELL2, LOW);
      delay(25);
      // Set hammer to other side
      digitalWrite(RINGERBELL1, LOW);
      digitalWrite(RINGERBELL2, HIGH);
      delay(25);  
    }
    //all off
    digitalWrite(RINGERBELL1, LOW);
    digitalWrite(RINGERBELL2, LOW);

 }

void readsettings() {
    EEPROM.get(addr,data);
    playvolume     = data.value1; //get volumeoffset from EEPROM
    foldertoplay   = data.value2; //get folder from EEPROM
    folderislocked = data.value3; //get status locked
    lockedfolder   = data.value4; //get locked folder
    randomenabled  = data.value5; //get status random-play
    if (foldertoplay<1||foldertoplay>10) {
      Serial.println("Foldertoplay value corrected to 1");
      foldertoplay=1;
    }
    if (playvolume<1||playvolume>30) {
      Serial.println("Volumeoffset value corrected to 24");
      playvolume=24;
    }    
    //Serial.println("* settings read");
    //Serial.print  ("Random: "); Serial.println(randomenabled);
}

void savesettings() {
    data.value1 = playvolume;                   //save volume
    data.value2 = foldertoplay;                 //save playfolder
    data.value3 = folderislocked;
    data.value4 = lockedfolder;
    data.value5 = randomenabled;
    EEPROM.put(addr,data);
    EEPROM.commit();
    //Serial.println("* settings saved");
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
      Serial.print(value+1);
      Serial.println(F(" Play Finished!"));
      playfinished = true;
      delay(500);
      //myDFPlayer.stop();
      state = S_WAITFORDIALERBUSY;
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
          Serial.println(F("File Index Out of Bounds"));
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
