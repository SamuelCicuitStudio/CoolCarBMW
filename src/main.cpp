/*
  Main logic: Welcome / CC-ID player / Seatbelt / Low-fuel reminder / Goodbye / Radio / KOMBI sweep.

  Priorities (higher wins):
     3 = Welcome
     2 = CC-ID (including generic gong 14)
     1 = Seatbelt (track 2 – loops while active)

  Requires:
    - CanBus.h  : doors + handbrake + key + sweep
    - Player.h  : DFPlayer wrapper
    - CCIDMap.h : trackForCcid(), isSeatbeltCCID(), low-fuel + low-batt helpers (we provide tiny inline fallbacks below)
*/

#include <Arduino.h>
#include "Pins.h"
#include "CanBus.h"
#include "Player.h"
#include "CCIDMap.h"

// -------------------- CONFIG --------------------
static const bool DEBUG_PRINTS = true;
static inline void DBG(const __FlashStringHelper* s){ if(DEBUG_PRINTS) Serial.println(s); }

static const uint32_t WELCOME_WINDOW_MS = 120000UL; // 2 minutes
#ifndef PIN_RADIO_HOLD
#define PIN_RADIO_HOLD 9  // GPIO9 for radio keep-alive
#endif

// -------------------- HELPERS --------------------
static inline bool timeBefore(uint32_t deadline){ return (int32_t)(millis() - (int32_t)deadline) < 0; }
static inline uint16_t rnd(uint16_t a, uint16_t b){ return a + (uint16_t)random((long)(b-a+1)); }


// -------------------- GLOBALS --------------------
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
bool radioHoldActive = false;        // keep radio HIGH until LOCK after KL15 off
bool radioUnlockedWaitingDoor = false; // after UNLOCK, set HIGH on first door open (unless low-batt CCID active)
bool lowBatteryActive = false;       // active low-batt CCID inhibits radio HIGH on unlock

// Handbrake track 50 predicate is evaluated on driver-door OPEN
// (welcome has higher priority and may preempt it)

// -------------------- CLI (optional) --------------------
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
          if(n>=1 && n<=DF_MAX_MP3){ if(player.isPlaying()) player.stop(); player.playTrack((uint16_t)n); }
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

// -------------------- PRIORITY ENGINE --------------------
enum class NowPlaying : uint8_t { None, Welcome, Ccid, Seatbelt };
NowPlaying nowPlaying = NowPlaying::None;

void playWelcome(){
  if(player.isPlaying()) player.stop();
  player.playTrack(1);
  nowPlaying = NowPlaying::Welcome;
  DBG(F("[PLAY] Welcome T1"));
}
void playCcid(uint16_t tr){
  if(player.isPlaying()) player.stop();
  player.playTrack(tr);
  nowPlaying = NowPlaying::Ccid;
  Serial.print(F("[PLAY] CCID T")); Serial.println(tr);
}
void ensureSeatbeltLoop(){
  if(!seatbeltActive) return;
  if(nowPlaying == NowPlaying::Welcome) return; // let welcome finish
  if(nowPlaying == NowPlaying::Ccid)    return; // CCID has higher priority
  if(!player.isPlaying() || player.currentTrack()!=2){
    if(player.isPlaying()) player.stop();
    player.playTrack(2);
    nowPlaying = NowPlaying::Seatbelt;
    DBG(F("[PLAY] Seatbelt T2 loop"));
  }
}
void stopIfTrack(uint16_t tr){
  if(player.isPlaying() && player.currentTrack()==tr) player.stop();
}

// -------------------- RADIO CONTROL --------------------
void radioSet(bool on){
  digitalWrite(PIN_RADIO_HOLD, on?HIGH:LOW);
  if(DEBUG_PRINTS){ Serial.print(F("[RADIO] ")); Serial.println(on?F("HIGH"):F("LOW")); }
}

// -------------------- SETUP --------------------
void setup(){
  Serial.begin(115200);
  delay(100);
  DBG(F("Starting main: Welcome/CCID/Seatbelt/Fuel/Goodbye/Radio + KOMBI"));

  // HW
  pinMode(PIN_RADIO_HOLD, OUTPUT);
  radioSet(false);

  if(!canbus.begin()){
    DBG(F("MCP2515 init FAIL"));
    while(1) delay(1000);
  }
  DBG(F("MCP2515 OK (8MHz, 100kbps)"));

  // KOMBI sweep as before
  canbus.enableSweep(true);
  canbus.setSweepAcceptACC(false);
  canbus.setSweepTargets(260, 5500);
  canbus.setSweepTiming(28, 35, 1000, 4000);

  // Player
  player.setBenchMode(false);
  player.begin();
  Serial.print(F("Player ready (vol=")); Serial.print(player.volume()); Serial.println(')');

  // Seed RNG from floating analog if desired
  randomSeed(analogRead(A0));
}

// -------------------- LOOP --------------------
void loop(){
  parseCLI();

  // 1) Drain CAN frames and feed CanBus (doors/handbrake/key/KL15(inside class))
  //    We also react to events via queues so we only act on changes.
  uint32_t id; uint8_t len; uint8_t buf[8];
  while (canbus.readOnceDistinct(id,len,buf)) {
    canbus.onFrame(id,len,buf);

    // Observe KL15 directly from 0x130 byte0 (same place class uses internally)
    if(id == ID_KL15 && len>=1){
      kl15Prev = kl15On;
      kl15On = ((buf[0] & 0x04)!=0) || ((buf[0] & 0x08)!=0) || ((buf[0] & 0x01)!=0 && false); // ACC optional
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

    // Parse CCID frames (0x338 ... FE FE FE)
    if(id == ID_CCID && len>=8 && buf[5]==0xFE && buf[6]==0xFE && buf[7]==0xFE){
      const uint16_t ccid = (uint16_t(buf[1])<<8) | buf[0];
      const uint8_t  st   = buf[2]; // 0x02 ACTIVE, 0x01 CLEARED

      if(isSeatbeltCCID(ccid)){
        if(st == 0x02){
          seatbeltActive = true;
          if(nowPlaying != NowPlaying::Welcome){ // welcome preempts everything
            stopIfTrack(1); // safety
            // Seatbelt is lowest priority; will loop after CCID finishes
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
          else if(st==0x01)       lowFuelSeenWhileIgnOn = false; // cleared while running
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
            // Priority 2 (above seatbelt, below welcome)
            if(nowPlaying == NowPlaying::Welcome){
              // queue is simple: let welcome finish; when player stops, we’ll pick up CCIDs on next activation only
              // Spec says welcome should come first; we skip playing CCID now.
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

  // 2) Handle Key events: UNLOCK/LOCK/TRUNK
  CanBus::KeyEvent kev;
  while (canbus.nextKeyEvent(kev)){
    if(kev.type == CanBus::KeyEventType::Unlock){
      // Welcome arm
      welcomeArmed   = true;
      welcomeHold    = false;
      welcomeDeadline = millis() + WELCOME_WINDOW_MS;
      passengerSeenSinceUnlock = false;
      radioUnlockedWaitingDoor = true;
      DBG(F("[KEY] UNLOCK -> welcome armed; radio waits door"));

    } else if(kev.type == CanBus::KeyEventType::Lock){
      // cancel radio hold
      radioHoldActive = false;
      radioUnlockedWaitingDoor = false;
      radioSet(false);
      DBG(F("[KEY] LOCK -> radio OFF; goodbye context cleared"));

    } else {
      // ignore TRUNK/OTHER for welcome
    }
  }

  // 3) Door changes (drive welcome/goodbye/handbrake + radio on first door open after unlock)
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
      // First door after unlock -> turn radio ON unless low-batt CCID active
      if(!lowBatteryActive){
        radioSet(true);
      }
      radioUnlockedWaitingDoor = false;
    }

    if(passengerOpen) passengerSeenSinceUnlock = true;

    // WELCOME (priority 3): only on driver door, and either within window or held by non-driver opens
    if(driverOpen && welcomeArmed && (welcomeHold || timeBefore(welcomeDeadline))){
      playWelcome();
      welcomeArmed = false; welcomeHold = false;
    } else if(anyOpen && welcomeArmed && !driverOpen){
      // hold welcome indefinitely until driver opens
      welcomeHold = true;
    }

    // HANDBRAKE T50 on driver door open (unless welcome just won)
    if(driverOpen && !canbus.handbrakeEngaged()){
      if(!welcomeArmed && nowPlaying != NowPlaying::Welcome){
        // Allowed when ignition off or on (engine not running not enforced here)
        if(player.isPlaying()) player.stop();
        player.playTrack(50);
        nowPlaying = NowPlaying::Ccid; // treat like an alert-level playback
        DBG(F("[PLAY] Handbrake warning T50"));
      }
    }

    // LOW-FUEL REMINDER T45 once after KL15 OFF on next driver-door open
    if(driverOpen && !kl15On && lowFuelRemindArmed){
      if(!(player.isPlaying() && player.currentTrack()==1)){ // if not welcome
        if(player.isPlaying()) player.stop();
        player.playTrack(45);
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
          if(player.isPlaying()) player.stop();
          player.playTrack(tr);
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

  // 7) Small idle
  delay(2);
}
