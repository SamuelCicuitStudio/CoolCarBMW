/*
  Main logic: Welcome / CC-ID player / Seatbelt / Low-fuel reminder / Goodbye / Radio / KOMBI sweep
  + Priority queue for deferred (lower-priority) sounds

  Priorities (higher wins):
     3 = Welcome
     2 = CC-ID (incl. generic gong 14, low-fuel T45, handbrake T50, goodbye 46-49)
     1 = Seatbelt (track 2 – loops while active)
*/

#include <Arduino.h>
#include "Pins.h"
#include "CanBus.h"
#include "Player.h"
#include "CCIDMap.h"

// ========= Radio Power Control =========
// The radio is NOT the DFPlayer. We hold its power via GPIO 9.
#ifndef PIN_RADIO_HOLD
#define PIN_RADIO_HOLD 9   // Keeps radio powered (HIGH = radio ON, LOW = radio OFF)
#endif

// =================== CONFIG ===================
static const bool DEBUG_PRINTS = true;
static inline void DBG(const __FlashStringHelper* s){ if(DEBUG_PRINTS) Serial.println(s); }

static const uint32_t WELCOME_WINDOW_MS = 120000UL; // 2 minutes

// =================== UTILS ===================
static inline bool timeBefore(uint32_t deadline){ return (int32_t)(millis() - (int32_t)deadline) < 0; }

// =================== GLOBALS ===================
CanBus canbus;
Player  player;

// Sport mode state (global)
bool sportMode = false;

// Welcome state
bool     welcomeArmed = false;
bool     welcomeHold  = false; // true if any non-driver door opened after unlock
uint32_t welcomeDeadline = 0;

// Passenger detection since unlock (for goodbye variants)
bool passengerSeenSinceUnlock = false;
// Global: passenger-seat occupancy (updated from AirBag frames)
bool passengerSeated = false;

// Confirm passenger only if seat becomes occupied AFTER passenger door opened
bool passengerEnterConfirmed = false;      // true means a real passenger was detected (door->then seat)
bool passengerSeatArmedByDoorOpen = false; // set when passenger door opens while seat is empty

// Seatbelt state
bool seatbeltActive = false;

// CC-ID once-per-activation
uint16_t lastCcid   = 0xFFFF;
uint8_t  lastStatus = 0xFF;  // 0x02 active, 0x01 cleared

// KL15 + low-fuel reminder
bool     kl15On = false;
bool     kl15Prev = false;
bool     lowFuelSeenWhileIgnOn = false; // set if low fuel CCID fired with KL15 ON
bool     lowFuelRemindArmed    = false; // armed at KL15->OFF; play track 45 once on next driver-door open

// --- KL15 raw tracking & engine-stop Goodbye arming ---
uint8_t  kl15Raw = 0x00;                 // last seen raw KL15 byte (e.g., 0x00, 0x40, 0x41, 0x45, 0x55)
uint8_t  kl15Hist[4] = {0,0,0,0};        // circular history (newest write index is kl15HistIdx-1)
uint8_t  kl15HistIdx = 0;                // increments on each new sample
bool     engineStopGoodbyeArmed = false; // set on RUN/CRANK -> {0x40,0x00} transition

static inline bool kl15IsRunVal(uint8_t v){ return (v==0x45) || (v==0x55); }
static inline void kl15Push(uint8_t v){
  kl15Raw = v;
  kl15Hist[kl15HistIdx & 3] = v;
  kl15HistIdx++;
}

// ===== Radio state (GPIO9) =====
static inline void radioSet(bool on){
  digitalWrite(PIN_RADIO_HOLD, on ? HIGH : LOW);
  if(DEBUG_PRINTS){ Serial.print(F("[RADIO] ")); Serial.println(on?F("HIGH"):F("LOW")); }
}
bool radioHeldAfterIgnOff = false;      // true: we are keeping radio on after KL15 went OFF
bool radioUnlockedWaitingDoor = false;  // legacy latch (neutralized by new policy)
bool lowBatteryActive = false;          // kept for compatibility with CCID tracking

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
            if(player.isPlaying()) player.stop();
            player.playTrack((uint16_t)n);
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

// ===== Pending audio queue (priority-based) =====
enum class AlertKind : uint8_t { Welcome, Ccid, LowFuel, Handbrake, Goodbye };
struct Alert {
  uint8_t   prio;        // 1..3 (3 highest)
  uint16_t  track;       // track to play
  AlertKind kind;        // type, for sanity/filters
  uint16_t  ccid;        // only for CCID (else 0)
  uint32_t  t_ms;        // when enqueued
};

static const uint8_t PQUEUE_CAP = 10;
Alert pqueue[PQUEUE_CAP];
uint8_t pqCount = 0;

// still-valid guard at playback time
static bool alertStillValid(const Alert& a){
  switch(a.kind){
    case AlertKind::Ccid:      return (lastStatus==0x02 && lastCcid==a.ccid);
    case AlertKind::Welcome:   return welcomeArmed && (welcomeHold || timeBefore(welcomeDeadline));
    case AlertKind::LowFuel:   return (!kl15On && lowFuelRemindArmed);
    case AlertKind::Handbrake: return !canbus.handbrakeEngaged();
    case AlertKind::Goodbye:   return (!kl15On && lastStatus != 0x02);
  }
  return true;
}
static void pqPush(uint8_t prio, uint16_t track, AlertKind kind, uint16_t ccid = 0){
  if(pqCount >= PQUEUE_CAP){ for(uint8_t i=1;i<pqCount;i++) pqueue[i-1]=pqueue[i]; pqCount--; }
  pqueue[pqCount++] = { prio, track, kind, ccid, millis() };
}
static bool pqPopNext(Alert &out){
  if(pqCount==0) return false;
  int idx=-1;
  for(uint8_t i=0;i<pqCount;i++){
    if(idx==-1) idx=i;
    else{
      if(pqueue[i].prio > pqueue[idx].prio) idx=i;
      else if(pqueue[i].prio == pqueue[idx].prio && (int32_t)(pqueue[i].t_ms - pqueue[idx].t_ms) < 0) idx=i;
    }
  }
  out = pqueue[idx];
  for(uint8_t i=idx+1;i<pqCount;i++) pqueue[i-1]=pqueue[i];
  pqCount--;
  return true;
}
static bool playNextFromQueue(){
  Alert a;
  while (pqPopNext(a)){
    if(alertStillValid(a)){
      if(player.isPlaying()) player.stop();
      player.playTrack(a.track);
      switch(a.kind){
        case AlertKind::Welcome:   nowPlaying = NowPlaying::Welcome;  break;
        case AlertKind::Ccid:      nowPlaying = NowPlaying::Ccid;     break;
        case AlertKind::LowFuel:   nowPlaying = NowPlaying::Ccid;     break;
        case AlertKind::Handbrake: nowPlaying = NowPlaying::Ccid;     break;
        case AlertKind::Goodbye:   nowPlaying = NowPlaying::Ccid;     break;
      }
      return true;
    }
  }
  return false;
}

// =================== SETUP ===================
void setup(){
  Serial.begin(115200);
  delay(100);
  DBG(F("Starting main: Welcome/CCID/Seatbelt/Fuel/Goodbye/Radio + KOMBI + Queue"));

  // Pins
  pinMode(PIN_RADIO_HOLD, OUTPUT);   radioSet(false);         // radio initially OFF
  pinMode(PIN_SPK_RELAY, OUTPUT);    digitalWrite(PIN_SPK_RELAY, LOW); // anti-pop

  // CAN
  if(!canbus.begin()){
    DBG(F("MCP2515 init FAIL")); while(1) delay(1000);
  }
  DBG(F("MCP2515 OK (8MHz, 100kbps)"));

  // KOMBI sweep setup
  canbus.enableSweep(true);
  canbus.setSweepAcceptACC(false);
  canbus.setSweepTargets(260, 5500);
  canbus.setSweepTiming(28, 35, 1000, 4000);

  // DFPlayer
  player.setBenchMode(false);
  player.begin(); // initializes pins; DF power remains OFF until playTrack() (handled in Player.cpp)

  // RNG
  randomSeed(analogRead(A0));
}

// =================== LOOP ===================
void loop(){
  parseCLI();

  // 1) CAN ingest
  uint32_t id; uint8_t len; uint8_t buf[8];
  while (canbus.readOnceDistinct(id,len,buf)) {
    canbus.onFrame(id,len,buf);

    // --- AirBag / Passenger detection (print only on change) ---
    if (id == ID_AIRBAG && len >= 2) {
      const bool newPassengerSeated = bitRead(buf[1], 3);  // bit 3 of second byte
      if (newPassengerSeated != passengerSeated) {
        passengerSeated = newPassengerSeated;
        if (passengerSeated) {
          DBG(F("[AirBag] Passenger seated"));
          if (passengerSeatArmedByDoorOpen) {
            passengerEnterConfirmed = true;
            passengerSeatArmedByDoorOpen = false; // consume the arm
            DBG(F("[AirBag] Passenger CONFIRMED (door->seat)"));
          }
        } else {
          DBG(F("[AirBag] Passenger not seated"));
        }
      }
    }

    // --- Sport Mode Toggle (CAN 0x315): buf[0]=0x82 (ignored), buf[1]=F1/F2 ---
    if (id == ID_BUTTON && len >= 2) {
      const uint8_t modeByte = buf[1];  // second byte defines sport state
      bool newSportMode = false;

      if (modeByte == 0xF2) newSportMode = true;    // Sport ON
      else if (modeByte == 0xF1) newSportMode = false; // Sport OFF

      if (newSportMode != sportMode) {
        sportMode = newSportMode;
        if (sportMode) {
          DBG(F("[BUTTON] Sport Mode ON"));
          // Play T52 only if ignition is ON and Welcome isn't playing. Preempts seatbelt only.
          if (kl15On && nowPlaying != NowPlaying::Welcome) {
            if (player.isPlaying() && player.currentTrack()==2) player.stop();
            if (!player.isPlaying()) {
              player.playTrack(52);
              nowPlaying = NowPlaying::Ccid;
              DBG(F("[PLAY] Sport ON T52"));
            }
          }
        } else {
          DBG(F("[BUTTON] Sport Mode OFF"));
          // Play T53 only if ignition is ON and Welcome isn't playing. Preempts seatbelt only.
          if (kl15On && nowPlaying != NowPlaying::Welcome) {
            if (player.isPlaying() && player.currentTrack()==2) player.stop();
            if (!player.isPlaying()) {
              player.playTrack(53);
              nowPlaying = NowPlaying::Ccid;
              DBG(F("[PLAY] Sport OFF T53"));
            }
          }
        }
      }
      // Repeated F1/F2 frames every ~5s are ignored because sportMode state didn't change.
    }

    // KL15 observe (same byte your CanBus uses)
    if(id == ID_KL15 && len>=1){
      const uint8_t v = buf[0];

      // detect RUN/CRANK -> {0x40,0x00} to arm Goodbye
      const bool wasRunLike = kl15IsRunVal(kl15Raw);
      const bool nowRunLike = kl15IsRunVal(v);
      kl15Push(v);

      // Existing boolean for other subsystems (sweep/etc)
      kl15Prev = kl15On;
      // RUN or START means ON (ACC not used here for sweep)
      kl15On = ((v & 0x04)!=0) || ((v & 0x08)!=0);

      // Arm goodbye only when engine was running and now is stopped (v==0x40 or 0x00)
      if (wasRunLike && !nowRunLike && (v==0x40 || v==0x00)){
        engineStopGoodbyeArmed = true;
        DBG(F("[KL15] Engine STOP (45/55 -> 40/00) => Goodbye ARMED"));
      }
      if (nowRunLike){
        engineStopGoodbyeArmed = false; // cancel if running again
      }

      // ===== Radio policy hooks =====
      if(kl15Prev && !kl15On){
        // Ignition just turned OFF → KEEP RADIO ON until Lock
        radioSet(true);
        radioHeldAfterIgnOff = true;
        DBG(F("[RADIO] Hold ON after KL15 OFF"));
        if(lowFuelSeenWhileIgnOn){
          lowFuelRemindArmed = true; // play 45 once on next driver-door open
        }
        DBG(F("[KL15] OFF; fuel remind armed?"));
      } else if(!kl15Prev && kl15On){
        // Ignition just turned ON → clear goodbye/remind arming, radio policy unchanged
        lowFuelRemindArmed = false;
        passengerSeenSinceUnlock = false;
        passengerEnterConfirmed = false;      // reset per trip
        passengerSeatArmedByDoorOpen = false; // reset the arm
        radioHeldAfterIgnOff = false;         // not in a post-off hold anymore
        DBG(F("[KL15] ON"));
        canbus.enableSweep(true);
      }
    }

    // CC-ID frames (0x338 ... FE FE FE)
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
        // Track low-fuel + low-battery (keep for compatibility)
        if(isLowFuelCCID(ccid)){
          if(st==0x02 && kl15On)  lowFuelSeenWhileIgnOn = true;
          else if(st==0x01)       lowFuelSeenWhileIgnOn = false;
        }
        if(isLowBatteryCCID(ccid)){
          lowBatteryActive = (st==0x02);
          // No longer inhibits radio-on; requirement says radio ON at unlock & held after OFF
        }

        // Play once per activation until cleared
        if(st == 0x02){
          const bool armed = !(lastCcid == ccid && lastStatus == 0x02);
          lastCcid = ccid; lastStatus = 0x02;
          if(armed){
            const uint16_t tr = trackForCcid(ccid); // default 14 for unknowns
            if(nowPlaying == NowPlaying::Welcome){
              // defer CC-ID while welcome is active
              pqPush(2, tr, AlertKind::Ccid, ccid);
            } else {
              // CC-ID preempts seatbelt
              stopIfTrack(2);
              playCcid(tr);
            }
          }
        } else if(st == 0x01){
          if(lastCcid == ccid) lastStatus = 0x01;
        }
      }
    }
  }

  // 2) Key events UNLOCK/LOCK (radio policy integrated)
  CanBus::KeyEvent kev;
  while (canbus.nextKeyEvent(kev)){
    if(kev.type == CanBus::KeyEventType::Unlock){
      welcomeArmed   = true;
      welcomeHold    = false;
      welcomeDeadline = millis() + WELCOME_WINDOW_MS;
      passengerSeenSinceUnlock = false;
      passengerEnterConfirmed = false;      // reset on unlock
      passengerSeatArmedByDoorOpen = false; // clear arm

      // New policy: ON at unlock immediately (no door wait)
      radioUnlockedWaitingDoor = false;     // neutralize legacy path
      radioHeldAfterIgnOff = false;         // now explicit unlock took control
      radioSet(true);
      DBG(F("[KEY] UNLOCK -> radio ON; welcome armed"));

    } else if(kev.type == CanBus::KeyEventType::Lock){
      // New policy: OFF at lock, also cancels any post-off hold
      radioSet(false);
      radioUnlockedWaitingDoor = false;
      radioHeldAfterIgnOff = false;
      DBG(F("[KEY] LOCK -> radio OFF"));
    }
  }

  // 3) Door changes (welcome/goodbye/handbrake + legacy radio latch is disabled)
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

    // Legacy behavior (radio on first door after unlock) is now bypassed.
    // We keep the variables for minimal perturbation; condition is never true.
    if(anyOpen && radioUnlockedWaitingDoor){
      // Previously: if(!lowBatteryActive){ radioSet(true); }
      // Now unreachable because radioUnlockedWaitingDoor is cleared at unlock.
      radioUnlockedWaitingDoor = false;
    }

    if(passengerOpen){
      passengerSeenSinceUnlock = true;
      // Arm a passenger confirmation only if the seat is currently empty;
      passengerSeatArmedByDoorOpen = !passengerSeated;
    }

    // WELCOME (priority 3): only on driver door, within window or held by non-driver opens
    // SUPPRESS welcome if a Goodbye is armed (engine just stopped)
    if(driverOpen && welcomeArmed && !engineStopGoodbyeArmed && (welcomeHold || timeBefore(welcomeDeadline))){
      playWelcome();
      welcomeArmed = false; welcomeHold = false;
    } else if(anyOpen && welcomeArmed && !driverOpen){
      // any non-driver open holds welcome until the driver door opens
      welcomeHold = true;
    }

    // HANDBRAKE T50 on driver door open (unless welcome just won)
    if(driverOpen && !canbus.handbrakeEngaged()){
      if(!welcomeArmed && nowPlaying != NowPlaying::Welcome){
        if(player.isPlaying()) player.stop();
        player.playTrack(50);
        nowPlaying = NowPlaying::Ccid;
        DBG(F("[PLAY] Handbrake warning T50"));
      } else if (nowPlaying == NowPlaying::Welcome){
        pqPush(2, 50, AlertKind::Handbrake);
      }
    }

    // LOW-FUEL REMINDER T45 once after KL15 OFF on next driver-door open
    if(driverOpen && !kl15On && lowFuelRemindArmed){
      if(!(player.isPlaying() && player.currentTrack()==1)){ // not welcome
        if (nowPlaying == NowPlaying::Welcome){
          pqPush(2, 45, AlertKind::LowFuel);
        } else {
          if(player.isPlaying()) player.stop();
          player.playTrack(45);
          nowPlaying = NowPlaying::Ccid;
          DBG(F("[PLAY] Low-fuel reminder T45"));
        }
        lowFuelRemindArmed = false; // single-shot
      }
    }

    // ======= GOODBYE: trigger ONLY on driver door after a stop; choose driver vs driver+passenger =======
    if(driverOpen && !kl15On && engineStopGoodbyeArmed){
      const bool anyCcidActive = (lastStatus==0x02);
      if(!anyCcidActive && !lowFuelRemindArmed){
        uint16_t tr = 0;

        if (passengerEnterConfirmed){
          tr = (random(0,2)==0) ? 48 : 49;   // goodbye driver & passenger / 2
        } else {
          tr = (random(0,2)==0) ? 46 : 47;   // goodbye driver / 2
        }

        if(tr){
          if (nowPlaying == NowPlaying::Welcome){
            pqPush(2, tr, AlertKind::Goodbye);
          } else {
            if(player.isPlaying()) player.stop();
            player.playTrack(tr);
            nowPlaying = NowPlaying::Ccid;
            Serial.print(F("[PLAY] Goodbye T")); Serial.println(tr);
          }
          engineStopGoodbyeArmed = false; // consume the armed Goodbye
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
  canbus.tickSweep();            // sweep state machine (KL15-controlled)
  player.loop();                 // detects end-of-track via BUSY pin

  // 7) If a track just finished, drain queue or resume seatbelt
  static bool wasPlaying = false;
  bool isNowPlaying = player.isPlaying();
  if (wasPlaying && !isNowPlaying) {
    if (!playNextFromQueue()) {
      ensureSeatbeltLoop();
      nowPlaying = NowPlaying::None;
    }
  }
  wasPlaying = isNowPlaying;

  delay(2);
}
