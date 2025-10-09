/*
  Main logic: Welcome / CC-ID player / Seatbelt / Low-fuel reminder / Goodbye / Radio / KOMBI sweep
  + DFPlayer MOSFET power via PIN_DF_EN (Pins.h)

  Priorities (higher wins):
     3 = Welcome
     2 = CC-ID (including generic gong 14)
     1 = Seatbelt (track 2 – loops while active)

  Select DFPlayer power policy:
    1 = Power ON right before playing audio; OFF after idle timeout
    2 = Power ON when CAN traffic is detected; OFF after CAN silence
*/
#include <Arduino.h>
#include "Pins.h"
#include "CanBus.h"
#include "Player.h"
#include "CCIDMap.h"

// =================== CONFIG ===================
#define DF_PWR_MODE 1          // <-- set to 1 or 2
static const bool DEBUG_PRINTS = true;

static const uint32_t WELCOME_WINDOW_MS = 120000UL; // 2 minutes
#ifndef PIN_RADIO_HOLD
#define PIN_RADIO_HOLD 9       // radio keep-alive
#endif

// DFPlayer power (MOSFET gate) comes from Pins.h
//   PIN_DF_EN: HIGH = ON, LOW = OFF  (see Pins.h)
static const uint32_t DF_WAKE_MS = 80;     // power-up settle for DFPlayer

// Mode 1 idle timeout (no audio playing)
static const uint32_t DF_IDLE_OFF_MS = 3000;

// Mode 2 CAN silence thresholds
static const uint32_t CAN_SILENCE_OFF_MS = 8000;
static const uint32_t CAN_WAKE_DEBOUNCE_MS = 50;

// =================== UTILS ===================
static inline void DBG(const __FlashStringHelper* s){ if(DEBUG_PRINTS) Serial.println(s); }
static inline bool timeBefore(uint32_t deadline){ return (int32_t)(millis() - (int32_t)deadline) < 0; }

// =================== GLOBALS ===================
CanBus canbus;
Player  player;

// Welcome state
bool     welcomeArmed = false;
bool     welcomeHold  = false; // true if any non-driver door opened after unlock
uint32_t welcomeDeadline = 0;

// Passenger detection since unlock (for goodbye variants)
bool passengerSeenSinceUnlock = false;

// Seatbelt state
bool seatbeltActive = false;

// CC-ID once-per-activation
uint16_t lastCcid   = 0xFFFF;
uint8_t  lastStatus = 0xFF;  // 0x02 active, 0x01 cleared

// KL15 + low-fuel reminder
bool     kl15On = false;
bool     kl15Prev = false;
bool     lowFuelSeenWhileIgnOn = false; // set if low fuel CCID fired with KL15 ON
bool     lowFuelRemindArmed    = false; // armed at KL15->OFF; play track 45 once on driver-door open

// Radio (GPIO9) + low-battery inhibit
bool radioHoldActive = false;          // keep radio HIGH until LOCK after KL15 off
bool radioUnlockedWaitingDoor = false; // after UNLOCK, set HIGH on first door open (unless low-batt CCID active)
bool lowBatteryActive = false;         // active low-batt CCID inhibits radio HIGH on unlock

// DFPlayer power helpers
static inline void dfpPower(bool on){
  digitalWrite(PIN_DF_EN, on ? HIGH : LOW);
  if(DEBUG_PRINTS){ Serial.print(F("[DFP_PWR] ")); Serial.println(on?F("ON"):F("OFF")); }
}
static uint32_t lastAudioUseMs = 0;
static inline void noteAudioUsed(){ lastAudioUseMs = millis(); }

#if DF_PWR_MODE==2
static uint32_t lastCanSeenMs = 0;
#endif

// =================== CLI (optional) ===================
void parseCLI(){
  static String line;
  while(Serial.available()){
    char c = (char)Serial.read();
    if(c=='\r'){ if(Serial.peek()=='\n') (void)Serial.read(); c='\n'; }
    if(c=='\n'){
      line.trim();
      if(line.length()){
        char cmd=line[0];
        if(cmd=='p' || cmd=='P'){
          String digits; for(uint16_t i=1;i<line.length();++i){ char ch=line[i]; if(ch>='0'&&ch<='9') digits+=ch; else if(ch!=' ') { digits=""; break; } }
          long n = digits.length()?digits.toInt():-1;
          if(n>=1 && n<=DF_MAX_MP3){
            #if DF_PWR_MODE==1
              dfpPower(true); delay(DF_WAKE_MS); player.begin();
            #elif DF_PWR_MODE==2
              if (digitalRead(PIN_DF_EN)==LOW){ dfpPower(true); delay(CAN_WAKE_DEBOUNCE_MS); player.begin(); }
            #endif
            if(player.isPlaying()) player.stop();
            player.playTrack((uint16_t)n); noteAudioUsed();
          }
        } else if(cmd=='v' || cmd=='V'){
          if(line.length()==2 && (line[1]=='?')){ Serial.print(F("[CLI] volume=")); Serial.println(player.volume()); }
          else if(line.length()==2 && (line[1]=='+'||line[1]=='-')){
            int cur=(int)player.volume(); if(line[1]=='+') cur++; else cur--; if(cur<0)cur=0; if(cur>Player::DF_VOLUME_MAX)cur=Player::DF_VOLUME_MAX;
            player.setVolume((uint8_t)cur); Serial.print(F("[CLI] volume=")); Serial.println(player.volume());
          } else {
            String digits; for(uint16_t i=1;i<line.length();++i){ char ch=line[i]; if(ch>='0'&&ch<='9') digits+=ch; else if(ch!=' ') { digits=""; break; } }
            long v = digits.length()?digits.toInt():-1;
            if(v>=0 && v<=Player::DF_VOLUME_MAX){ player.setVolume((uint8_t)v); Serial.print(F("[CLI] volume set ")); Serial.println(player.volume()); }
          }
        }
      }
      line="";
    } else { line += c; if(line.length()>40) line.remove(0, line.length()-40); }
  }
}

// =================== PRIORITY ENGINE ===================
enum class NowPlaying : uint8_t { None, Welcome, Ccid, Seatbelt };
NowPlaying nowPlaying = NowPlaying::None;

static inline void ensureDfpOnBeforePlay(){
#if DF_PWR_MODE==1
  if (digitalRead(PIN_DF_EN)==LOW){ dfpPower(true); delay(DF_WAKE_MS); player.begin(); }
#elif DF_PWR_MODE==2
  if (digitalRead(PIN_DF_EN)==LOW){ dfpPower(true); delay(CAN_WAKE_DEBOUNCE_MS); player.begin(); }
#endif
}

void playWelcome(){
  ensureDfpOnBeforePlay();
  if(player.isPlaying()) player.stop();
  player.playTrack(1); noteAudioUsed();
  nowPlaying = NowPlaying::Welcome;
  DBG(F("[PLAY] Welcome T1"));
}
void playCcid(uint16_t tr){
  ensureDfpOnBeforePlay();
  if(player.isPlaying()) player.stop();
  player.playTrack(tr); noteAudioUsed();
  nowPlaying = NowPlaying::Ccid;
  Serial.print(F("[PLAY] CCID T")); Serial.println(tr);
}
void ensureSeatbeltLoop(){
  if(!seatbeltActive) return;
  if(nowPlaying == NowPlaying::Welcome) return; // let welcome finish
  if(nowPlaying == NowPlaying::Ccid)    return; // CCID has higher priority
  ensureDfpOnBeforePlay();
  if(!player.isPlaying() || player.currentTrack()!=2){
    if(player.isPlaying()) player.stop();
    player.playTrack(2); noteAudioUsed();
    nowPlaying = NowPlaying::Seatbelt;
    DBG(F("[PLAY] Seatbelt T2 loop"));
  }
}
void stopIfTrack(uint16_t tr){
  if(player.isPlaying() && player.currentTrack()==tr) player.stop();
}

// =================== RADIO CONTROL ===================
static inline void radioSet(bool on){
  digitalWrite(PIN_RADIO_HOLD, on?HIGH:LOW);
  if(DEBUG_PRINTS){ Serial.print(F("[RADIO] ")); Serial.println(on?F("HIGH"):F("LOW")); }
}

// =================== SETUP ===================
void setup(){
  Serial.begin(115200);
  delay(100);
  DBG(F("Starting main: Welcome/CCID/Seatbelt/Fuel/Goodbye/Radio + KOMBI + DF MOSFET"));

  // Pins
  pinMode(PIN_RADIO_HOLD, OUTPUT);   radioSet(false);
  pinMode(PIN_DF_EN, OUTPUT);        dfpPower(false);   // DFPlayer off initially
  pinMode(PIN_SPK_RELAY, OUTPUT);    digitalWrite(PIN_SPK_RELAY, LOW); // speaker relay off to avoid pops

  // CAN
  if(!canbus.begin()){
    DBG(F("MCP2515 init FAIL"));
    while(1) delay(1000);
  }
  DBG(F("MCP2515 OK (8MHz, 100kbps)"));

  // KOMBI sweep setup
  canbus.enableSweep(true);
  canbus.setSweepAcceptACC(false);
  canbus.setSweepTargets(260, 5500);
  canbus.setSweepTiming(28, 35, 1000, 4000);

  // DFPlayer
  player.setBenchMode(false);
  // NOTE: Player.begin() is called after we power the MOSFET (see play paths)

  // RNG
  randomSeed(analogRead(A0));
}

// =================== LOOP ===================
void loop(){
  parseCLI();

  // 1) CAN ingest
  uint32_t id; uint8_t len; uint8_t buf[8];
  while (canbus.readOnceDistinct(id,len,buf)) {
#if DF_PWR_MODE==2
    // Wake DFPlayer upon first CAN seen (if off), then init it
    lastCanSeenMs = millis();
    if (digitalRead(PIN_DF_EN)==LOW){ dfpPower(true); delay(CAN_WAKE_DEBOUNCE_MS); player.begin(); }
#endif
    canbus.onFrame(id,len,buf);

    // KL15 observe (same byte your CanBus uses)
    if(id == ID_KL15 && len>=1){
      kl15Prev = kl15On;
      kl15On = ((buf[0] & 0x04)!=0) || ((buf[0] & 0x08)!=0); // RUN/START considered ON
      if(kl15Prev && !kl15On){
        // Ignition just turned OFF
        radioHoldActive = true;
        radioSet(true); // keep radio alive until LOCK
        if(lowFuelSeenWhileIgnOn){
          lowFuelRemindArmed = true; // play 45 once on next driver-door open
        }
        DBG(F("[KL15] OFF; radio hold ON; fuel remind armed?"));
      } else if(!kl15Prev && kl15On){
        // Ignition just turned ON -> reset goodbye + remind arming
        lowFuelRemindArmed = false;
        passengerSeenSinceUnlock = false;
        DBG(F("[KL15] ON"));
      }
    }

    //  CC-ID frames (0x338 ... FE FE FE)
    if(id == ID_CCID && len>=8 && buf[5]==0xFE && buf[6]==0xFE && buf[7]==0xFE){
      const uint16_t ccid = (uint16_t(buf[1])<<8) | buf[0];
      const uint8_t  st   = buf[2]; // 0x02 ACTIVE, 0x01 CLEARED

      if(isSeatbeltCCID(ccid)){
        if(st == 0x02){
          seatbeltActive = true;
          if(nowPlaying != NowPlaying::Welcome){
            stopIfTrack(1);
            ensureSeatbeltLoop();
          }
        } else if(st == 0x01){
          seatbeltActive = false;
          stopIfTrack(2);
          if(nowPlaying == NowPlaying::Seatbelt) nowPlaying = NowPlaying::None;
        }
      } else {
        // Low-fuel + low-battery tracking
        if(isLowFuelCCID(ccid)){
          if(st==0x02 && kl15On)  lowFuelSeenWhileIgnOn = true;
          else if(st==0x01)       lowFuelSeenWhileIgnOn = false;
        }
        if(isLowBatteryCCID(ccid)){
          lowBatteryActive = (st==0x02);
          if(lowBatteryActive) radioSet(false); // inhibit radio on low-batt
        }

        // Play once per activation until cleared
        if(st == 0x02){
          const bool armed = !(lastCcid == ccid && lastStatus == 0x02);
          lastCcid = ccid; lastStatus = 0x02;
          if(armed){
            const uint16_t tr = trackForCcid(ccid); // default 14 for unknowns
            if(nowPlaying == NowPlaying::Welcome){
              // Welcome first; CC-ID will replay if still active later (once cleared/active again)
            } else {
              stopIfTrack(2); // preempt seatbelt
              playCcid(tr);
            }
          }
        } else if(st == 0x01){
          if(lastCcid == ccid) lastStatus = 0x01;
        }
      }
    }
  }

  // 2) Key events UNLOCK/LOCK
  CanBus::KeyEvent kev;
  while (canbus.nextKeyEvent(kev)){
    if(kev.type == CanBus::KeyEventType::Unlock){
      welcomeArmed   = true;
      welcomeHold    = false;
      welcomeDeadline = millis() + WELCOME_WINDOW_MS;
      passengerSeenSinceUnlock = false;
      radioUnlockedWaitingDoor = true;
      DBG(F("[KEY] UNLOCK -> welcome armed; radio waits door"));
    } else if(kev.type == CanBus::KeyEventType::Lock){
      radioHoldActive = false;
      radioUnlockedWaitingDoor = false;
      radioSet(false);
      DBG(F("[KEY] LOCK -> radio OFF; goodbye context cleared"));
    }
  }

  // 3) Door changes (welcome/goodbye/handbrake + radio on first door after unlock)
  CanBus::DoorEvent dev;
  while (canbus.nextDoorEvent(dev)){
    const bool driverOpen =
      (dev.type==CanBus::DoorEventType::DriverOpened);
    const bool passengerOpen =
      (dev.type==CanBus::DoorEventType::PassengerOpened);
    const bool anyOpen = driverOpen || passengerOpen ||
      dev.type==CanBus::DoorEventType::RearDriverOpened ||
      dev.type==CanBus::DoorEventType::RearPassengerOpened ||
      dev.type==CanBus::DoorEventType::BootOpened ||
      dev.type==CanBus::DoorEventType::BonnetOpened;

    if(anyOpen && radioUnlockedWaitingDoor){
      if(!lowBatteryActive){ radioSet(true); }
      radioUnlockedWaitingDoor = false;
    }

    if(passengerOpen) passengerSeenSinceUnlock = true;

    // WELCOME (priority 3): only on driver door, within window or held by non-driver opens
    if(driverOpen && welcomeArmed && (welcomeHold || timeBefore(welcomeDeadline))){
      playWelcome();
      welcomeArmed = false; welcomeHold = false;
    } else if(anyOpen && welcomeArmed && !driverOpen){
      // any non-driver open holds welcome until the driver door opens
      welcomeHold = true;
    }

    // HANDBRAKE T50 on driver door open (unless welcome just won)
    if(driverOpen && !canbus.handbrakeEngaged()){
      if(!welcomeArmed && nowPlaying != NowPlaying::Welcome){
        ensureDfpOnBeforePlay();
        if(player.isPlaying()) player.stop();
        player.playTrack(50); noteAudioUsed();
        nowPlaying = NowPlaying::Ccid;
        DBG(F("[PLAY] Handbrake warning T50"));
      }
    }

    // LOW-FUEL REMINDER T45 once after KL15 OFF on next driver-door open
    if(driverOpen && !kl15On && lowFuelRemindArmed){
      if(!(player.isPlaying() && player.currentTrack()==1)){ // if not welcome
        ensureDfpOnBeforePlay();
        if(player.isPlaying()) player.stop();
        player.playTrack(45); noteAudioUsed();
        nowPlaying = NowPlaying::Ccid;
        DBG(F("[PLAY] Low-fuel reminder T45"));
        lowFuelRemindArmed = false; // single-shot
      }
    }

    // GOODBYE variants when key out and no CCID active
    if(anyOpen && !kl15On){
      const bool anyCcidActive = (lastStatus==0x02);
      if(!anyCcidActive && !lowFuelRemindArmed){
        uint16_t tr = 0;
        if(driverOpen || passengerOpen){
          if(passengerSeenSinceUnlock){
            tr = (random(0,2)==0) ? 48 : 49;
          } else if(driverOpen){
            tr = (random(0,2)==0) ? 46 : 47;
          }
        }
        if(tr){
          ensureDfpOnBeforePlay();
          if(player.isPlaying()) player.stop();
          player.playTrack(tr); noteAudioUsed();
          nowPlaying = NowPlaying::Ccid;
          Serial.print(F("[PLAY] Goodbye T")); Serial.println(tr);
        }
      }
    }
  }

  // 4) Welcome expiry (if not held by non-driver opens)
  if(welcomeArmed && !welcomeHold && !timeBefore(welcomeDeadline)){
    welcomeArmed = false;
    DBG(F("[WELCOME] window expired"));
  }

  // 5) Seatbelt loop (lowest priority)
  ensureSeatbeltLoop();

  // 6) KOMBI sweep heartbeat + DFPlayer service
  canbus.tickSweep();
  player.loop();

  // 7) DFPlayer power management according to mode
#if DF_PWR_MODE==1
  if(!player.isPlaying() && (millis() - lastAudioUseMs) > DF_IDLE_OFF_MS){
    if (digitalRead(PIN_DF_EN)==HIGH){ dfpPower(false); }
  }
#elif DF_PWR_MODE==2
  if ((millis() - lastCanSeenMs) > CAN_SILENCE_OFF_MS && !player.isPlaying()){
    if (digitalRead(PIN_DF_EN)==HIGH){ dfpPower(false); }
  }
#endif

  delay(2);
}
