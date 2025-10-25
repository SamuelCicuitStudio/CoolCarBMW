#include "Filter.h"

// --- One-time init and KOMBI sweep config (kept disabled at boot) ---
bool Filter::begin(){
  if(!_can.begin()) return false;

  // KOMBI sweep configuration lives here (not in main)
  _can.enableSweep(false);            // start disabled; avoid hot-plug sweep
  _can.setSweepAcceptACC(false);
  _can.setSweepTargets(260, 5500);
  _can.setSweepTiming(28, 35, 1000, 4000);

  _kl15Prev = _S.kl15On = _can.kl15On();
  return true;
}

// --- CC-ID classification: A1/A2/A3 vs Notification ---
Filter::EvClass Filter::classifyCcid(uint16_t ccid, uint8_t& prio){
  // A1: Stop-Now
  if (ccid==30 || ccid==33 || ccid==39 || ccid==212 || ccid==427 || ccid==961 || ccid==568 ||
      ccid==257 || ccid==367 || ccid==74) { prio=3; return EvClass::SecA1; }

  // A2: Drive-Now (seatbelt + doors/hood/trunk)
  if (ccid==46 || ccid==91 || ccid==389 || ccid==390 ||
      ccid==14 || ccid==15 || ccid==16 || ccid==17 || ccid==18 || ccid==19) { prio=2; return EvClass::SecA2; }

  // A3: High-severity
  if (ccid==29 || ccid==31 || ccid==49 || ccid==216 ||
      ccid==213 ||
      ccid==35 || ccid==236 || ccid==237 || ccid==382 ||
      ccid==166 ||
      ccid==306 || ccid==304 || ccid==229 || ccid==220 || ccid==415 ||
      ccid==139 || ccid==143 || ccid==140 || ccid==141 ||
      ccid==608 || ccid==609 || ccid==610 || ccid==611 ||
      ccid==142 || ccid==265 ||
      ccid==63  || ccid==384) { prio=1; return EvClass::SecA3; }

  prio=0; return EvClass::Notif; // A4 + B
}

// --- Queue posting helper ---
void Filter::post(EvClass cls, Kind k, uint16_t track, uint16_t ccid, uint8_t prio){
  PlayIntent e{ k, track, ccid, prio, millis() };
  if (cls == EvClass::Notif) { (void)_notQ.push(e); return; }
  (void)_secQ.push(e);
}

// --- Voltage update (0x3B4) -> battery flags ---
void Filter::updateVoltage(float v){
  _S.batteryV = v;
  bool low = (v < _batLow);
  if (low != _S.batteryLow){
    _S.batteryLow = low;
    // Sound for battery low comes via CC-ID mapping (T22); this just sets the level flag.
  }
}

// --- CC-ID frame handling: emit play-intents once per activation + mirrors ---
void Filter::handleCcid(uint16_t ccid, uint8_t st){
   // Special-case: some cars send CC-ID 0 as CLEARED (0x01) to indicate “All OK”.
  if (ccid == 0 && st == 0x01) {
    // Play it once when transitioning out of a non-zero CC-ID context.
    if (_lastCcid != 0 || _lastStatus == 0x02) {
      post(EvClass::Notif, Kind::Ccid, trackForCcid(0), 0, /*prio*/0); // -> track 23
      _lastCcid = 0; _lastStatus = 0x01;
      return;
    }
  }
  if (st == 0x02) {  // ACTIVE
    const bool armed = !(_lastCcid==ccid && _lastStatus==0x02);
    _lastCcid = ccid; _lastStatus = 0x02;
    _S.anyCcidActive = true;
    if (!armed) return;

    // mirrors
    if (isSeatbeltCCID(ccid)) _S.seatbeltActive = true;
    if (isLowFuelCCID(ccid) && _S.kl15On) _lowFuelSeenWhileIgnOn = true;

    uint8_t pr; EvClass cls = classifyCcid(ccid, pr);
    const uint16_t tr = trackForCcid(ccid); // from CCIDMap.h
    post(cls, Kind::Ccid, tr, ccid, pr);

  } else if (st == 0x01) { // CLEARED
    if (_lastCcid == ccid) _lastStatus = 0x01;

    if (isSeatbeltCCID(ccid)) _S.seatbeltActive = false;

    // recompute “any active” cheaply: if last status cleared, assume false until next activation
    _S.anyCcidActive = false;
  }
}

// --- Key / Door policies (Welcome / Goodbye / Fuel reminder) ---
void Filter::handleKeyDoor(){
  // Key events
  CanBus::KeyEvent kev;
  while (_can.nextKeyEvent(kev)){
    if(kev.type == CanBus::KeyEventType::Unlock){
      _welcomeArmed   = true;
      _welcomeHold    = false;
      _welcomeDeadline = millis() + 120000UL; // 2 min window
      _S.passengerSeenSinceUnlock = false;
    } else if(kev.type == CanBus::KeyEventType::Lock){
      // nothing special here; radio policy is handled in Device
    }
  }

  // Door events
  CanBus::DoorEvent dev;
  while (_can.nextDoorEvent(dev)){
    const bool driverOpen =
      (dev.type==CanBus::DoorEventType::DriverOpened);
    const bool passengerOpen =
      (dev.type==CanBus::DoorEventType::PassengerOpened);
    const bool anyOpen = driverOpen || passengerOpen ||
      dev.type==CanBus::DoorEventType::RearDriverOpened ||
      dev.type==CanBus::DoorEventType::RearPassengerOpened ||
      dev.type==CanBus::DoorEventType::BootOpened ||
      dev.type==CanBus::DoorEventType::BonnetOpened;

    if(passengerOpen) _S.passengerSeenSinceUnlock = true;

    // Welcome (high priority notification): driver door within window or held
    if(driverOpen && _welcomeArmed && (_welcomeHold || (int32_t)(millis() - (int32_t)_welcomeDeadline) < 0)){
      postNotif(Kind::Welcome, 1);
      _welcomeArmed = false; _welcomeHold = false;
      continue;
    } else if(anyOpen && _welcomeArmed && !driverOpen){
      _welcomeHold = true;
    }

    // Low-fuel reminder T45 once after KL15 OFF on next driver door open
    if(driverOpen && !_S.kl15On && _S.lowFuelRemindArmed){
      postNotif(Kind::FuelReminder, 45);
      _S.lowFuelRemindArmed = false;
    }

    // Goodbye: trigger ONLY on driver door after a stop (and no active CC-ID)
    if(driverOpen && !_S.kl15On && _engineStopGoodbyeArmed){
      if(!_S.anyCcidActive && !_S.lowFuelRemindArmed){
        uint16_t tr = _S.passengerSeenSinceUnlock
                      ? (random(0,2)==0 ? 48 : 49)
                      : (random(0,2)==0 ? 46 : 47);
        postNotif(Kind::Goodbye, tr);
        _engineStopGoodbyeArmed = false; // consume
      }
    }
  }

  // Welcome expiry
  if(_welcomeArmed && !_welcomeHold && (int32_t)(millis() - (int32_t)_welcomeDeadline) >= 0){
    _welcomeArmed = false;
  }
}

// --- Frame router: feed CanBus decoders, mirror state, publish edges ---
void Filter::handleFrame(uint32_t id, uint8_t len, const uint8_t* buf){
  // Keep CanBus internal snapshots in sync (doors, handbrake, KL15, sport, voltage, etc.)
  _can.onFrame(id, len, buf);

  // Mirror minimal car state for clients
  _S.kl15On      = _can.kl15On();
  _S.handbrakeUp = _can.handbrakeEngaged();
  _S.driverDoor  = _can.doorState().driver;
  if (_can.voltageValid()) updateVoltage(_can.lastVoltage());

  // 1) CC-ID frames (project format: last 3 bytes FE FE FE)
  if (id == ID_CCID && len>=8 && buf[5]==0xFE && buf[6]==0xFE && buf[7]==0xFE){
    const uint16_t ccid = (uint16_t(buf[1])<<8) | buf[0];
    const uint8_t  st   = buf[2]; // 0x02 ACTIVE, 0x01 CLEARED
    handleCcid(ccid, st);
    return;
  }

  // 2) Sport button (ON/OFF)
  if (id == ID_BUTTON && len >= 2){
    const uint8_t modeByte = buf[1];
    if (modeByte == 0xF2){ _S.sportMode = true;  postNotif(Kind::SportOn,  52); }
    else if (modeByte == 0xF1){ _S.sportMode = false; postNotif(Kind::SportOff, 53); }
    return;
  }

  // 3) KL15 processing: Ignition gong + Sweep gating + handbrake warn + edge policies
  if (id == ID_KL15 && len>=1){
    const bool on = _can.kl15On();

    // Ignition gong (T13) once per KL15 cycle
    if (on && !_ignGongPlayed){
      postNotif(Kind::IgnGong, 13);
      _ignGongPlayed = true;
    } else if(!on){
      _ignGongPlayed = false;
    }

    // Sweep gating: allow sweep only after we have seen KL15 OFF once since boot
    if (!on) {
      _sawOffSinceBoot = true;
    } else {
      _can.enableSweep(_sawOffSinceBoot);   // legit OFF->ON -> allow one sweep cycle
    }

    // Edge detection for OFF/ON to arm policies
    if (_kl15Prev && !on){
      // KL15 just turned OFF
      if(_lowFuelSeenWhileIgnOn){
        _S.lowFuelRemindArmed = true;   // will play 45 on next driver door open
        _lowFuelSeenWhileIgnOn = false;
      }
      _engineStopGoodbyeArmed = true;

      // handbrake down reminder (immediate)
      if (!_can.handbrakeEngaged()){
        post(EvClass::SecA2, Kind::HandbrakeWarn, 50, 0, /*prio*/2);
      }
    } else if(!_kl15Prev && on){
      // KL15 just turned ON -> clear goodbye/reminder arming
      _S.lowFuelRemindArmed = false;
      _engineStopGoodbyeArmed = false;
      _S.passengerSeenSinceUnlock = false;
    }
    _kl15Prev = on;

    return;
  }

  // 4) Voltage/engine state on 0x3B4 handled in CanBus::onFrame -> mirrored above
}

// --- Pump CAN + process policies + run KOMBI sweep machine ---
void Filter::tick(){
  uint32_t id; uint8_t len; uint8_t buf[8];
  while (_can.readOnceDistinct(id, len, buf)) {
    handleFrame(id, len, buf);
  }

  // Consume key/door streams to generate Welcome/Goodbye/Fuel intents
  handleKeyDoor();

  // KOMBI sweep state machine
  _can.tickSweep();
}
