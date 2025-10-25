#pragma once
#include <Arduino.h>
#include "CanBus.h"
#include "CCIDMap.h"   // trackForCcid(), isSeatbeltCCID(), isLowFuelCCID()

class Filter {
public:
  explicit Filter(uint8_t canCsPin = PIN_CAN_CS) : _can(canCsPin) {}

  // One-time boot init: starts MCP2515 and configures KOMBI sweep (kept disabled at boot).
  bool begin();

  // Call every loop: drains CAN, updates state, processes key/door policies, runs KOMBI sweep.
  void tick();

  // Optional threshold for battery low (volts)
  void setBatteryLowVolt(float v) { _batLow = v; }

  // === Read-only snapshot ===
  struct CarState {
    bool    kl15On      = false;
    bool    driverDoor  = false;
    bool    handbrakeUp = true;

    float   batteryV    = NAN;    // last 0x3B4 volts
    bool    batteryLow  = false;  // derived vs _batLow

    bool    seatbeltActive = false;       // from CC-ID seatbelt
    bool    sportMode      = false;       // from 0x315 F2/F1
    bool    passengerSeenSinceUnlock = false;

    bool    lowFuelRemindArmed = false;   // will fire once on next driver door (Filter emits the intent)
    bool    anyCcidActive = false;        // convenience for “is something alarming active?”
  };
  const CarState& state() const { return _S; }

  // === Play intents (resolved DFPlayer tracks) ===
  enum class Kind : uint8_t {
    Ccid, SportOn, SportOff, IgnGong, HandbrakeWarn,
    FuelReminder, Welcome, Goodbye
  };
  struct PlayIntent {
    Kind     kind;
    uint16_t track;     // DFPlayer track to play (already mapped)
    uint16_t ccid;      // for Kind::Ccid; otherwise 0
    uint8_t  prio;      // 3=A1, 2=A2, 1=A3, 0=Notif
    uint32_t t_ms;
  };

  // Two queues: Security (A1/A2/A3) and Notification (A4+B+Welcome/Goodbye)
  bool popSecurity(PlayIntent& out)     { return _secQ.pop(out); }
  bool popNotification(PlayIntent& out) { return _notQ.pop(out); }

  // You may still want key/door streams for radio policy in Device:
  bool nextKeyEvent(CanBus::KeyEvent& e)  { return _can.nextKeyEvent(e); }
  bool nextDoorEvent(CanBus::DoorEvent& e){ return _can.nextDoorEvent(e); }

private:
  // ===== tiny ring queue =====
  template<size_t CAP>
  struct RQ {
    PlayIntent q[CAP]; uint8_t w=0, r=0;
    bool push(const PlayIntent& e){ uint8_t n=(uint8_t)(w+1u)%CAP; if(n==r) return false; q[w]=e; w=n; return true; }
    bool pop(PlayIntent& e){ if(r==w) return false; e=q[r]; r=(uint8_t)(r+1u)%CAP; return true; }
  };

  // Security classes
  enum class EvClass : uint8_t { SecA1=3, SecA2=2, SecA3=1, Notif=0 };
  static EvClass classifyCcid(uint16_t ccid, uint8_t& prio);

  // Internals
  void handleFrame(uint32_t id, uint8_t len, const uint8_t* buf);
  void handleCcid(uint16_t ccid, uint8_t st);
  void handleKeyDoor();                 // NEW: welcome/goodbye/fuel reminder here
  void post(EvClass cls, Kind k, uint16_t track, uint16_t ccid=0, uint8_t prio=0);
  void postNotif(Kind k, uint16_t track){ post(EvClass::Notif, k, track); }
  void updateVoltage(float v);

  // members
  CanBus   _can;
  CarState _S;
  float    _batLow = 11.8f;

  // sweep gating / ignition gong
  bool     _ignGongPlayed   = false;   // once per KL15 cycle
  bool     _sawOffSinceBoot = false;   // sweep only after real OFF->ON
  bool     _kl15Prev        = false;

  // welcome/goodbye/fuel
  bool     _welcomeArmed = false;
  bool     _welcomeHold  = false;
  uint32_t _welcomeDeadline = 0;
  bool     _engineStopGoodbyeArmed = false;
  bool     _lowFuelSeenWhileIgnOn  = false;

  // CCID debounce + mirrors
  uint16_t _lastCcid = 0;
  uint8_t  _lastStatus = 0;   // 0x02 active, 0x01 cleared

  // queues
  RQ<24> _secQ;   // A1/A2/A3
  RQ<32> _notQ;   // A4 + B + Welcome/Goodbye
};
