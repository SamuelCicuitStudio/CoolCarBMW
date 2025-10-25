#pragma once
#include <Arduino.h>
#include <SPI.h>
#include "mcp_can.h"
#include "Pins.h"


class CanBus {
public:
  explicit CanBus(uint8_t csPin = PIN_CAN_CS);

  bool begin();

  // Read next *distinct* frame (de-duplicated within a small time window)
  bool readOnceDistinct(uint32_t &id, uint8_t &len, uint8_t *buf);

  // Feed every received frame here; updates snapshots, queues change events, keeps sweep state.
  void onFrame(uint32_t id, uint8_t len, const uint8_t *buf);

  // KOMBI sweep tick
  void tickSweep();

  // ---- KOMBI sweep config (unchanged) ----
  void enableSweep(bool on) { _sweepEnabled = on; }
  void setSweepAcceptACC(bool on) { _sweepAcceptACC = on; }
  void setSweepTargets(uint16_t kmh, uint16_t rpm){ _targetKmh = kmh; _targetRpm = rpm; }
  void setSweepTiming(uint16_t speedDelayMs, uint16_t rpmDelayMs, uint16_t peakDwellMs, uint16_t startAfterMs){
    _spdDelay = speedDelayMs; _rpmDelay = rpmDelayMs; _peakDwell = peakDwellMs; _startAfterKL15 = startAfterMs;
  }
  void setDedupWindow(uint16_t ms) { _dedupWindowMs = ms; }
  void setHistoryDepth(uint8_t depth) { _historyDepth = (depth>10)?10:depth; }

  // ================== STATE API ==================

  // Doors snapshot (read anytime)
  struct DoorSnapshot {
    bool driver;
    bool passenger;
    bool rearDriver;
    bool rearPassenger;
    bool boot;
    bool bonnet;
  };
  DoorSnapshot doorState() const { return _door; }

  // Handbrake snapshot
  bool handbrakeEngaged() const { return _handbrakeEngaged; }

  // Key/Lock snapshot
  enum class LockState : uint8_t { Unknown=0, Locked, Unlocked };
  struct KeySnapshot {
    uint8_t   raw;        // last raw key code (buf[2])
    LockState lockState;  // current lock state
    uint32_t  t_ms;       // when last key code accepted
  };
  KeySnapshot keyState() const { return _keySnap; }
  void setKeyCooldown(uint16_t ms){ _keyCooldownMs = ms; }  // default 250 ms

  // ================== CHANGE EVENTS ==================
  enum class DoorEventType : uint8_t {
    DriverOpened, DriverClosed,
    PassengerOpened, PassengerClosed,
    RearDriverOpened, RearDriverClosed,
    RearPassengerOpened, RearPassengerClosed,
    BootOpened, BootClosed,
    BonnetOpened, BonnetClosed
  };
  struct DoorEvent { DoorEventType type; uint32_t t_ms; };
  bool nextDoorEvent(DoorEvent &ev);

  enum class HandbrakeEventType : uint8_t { Engaged, Released };
  struct HandbrakeEvent { HandbrakeEventType type; uint32_t t_ms; };
  bool nextHandbrakeEvent(HandbrakeEvent &ev);

  enum class KeyEventType : uint8_t { Unlock, Lock, Trunk, Other };
  struct KeyEvent { KeyEventType type; uint32_t t_ms; };
  bool nextKeyEvent(KeyEvent &ev);
  // ---- Voltage (0x3B4) ----
  bool  readVoltage(float& outV, uint16_t timeoutMs = 100); // blocking helper (wait up to timeout)
  bool  voltageValid() const { return _voltageSeen; }        // have we seen a 0x3B4 yet?
  float lastVoltage() const { return _lastVoltage; }         // latest decoded voltage
  bool kl15On() const { return _kl15On; };

private:
  // ===== raw + de-dup =====
  MCP_CAN _can;
  struct FrameRec { uint32_t id; uint8_t len; uint8_t data[8]; uint32_t t; bool valid; };
  static const uint8_t MAX_HISTORY = 10;
  FrameRec _hist[MAX_HISTORY];
  uint8_t  _histHead;
  uint8_t  _historyDepth;
  uint16_t _dedupWindowMs;

  bool readRaw(uint32_t &id, uint8_t &len, uint8_t *buf);
  bool isDuplicate(uint32_t id, uint8_t len, const uint8_t *buf, uint32_t now) const;
  void pushHistory(uint32_t id, uint8_t len, const uint8_t *buf, uint32_t now);

  // ===== KOMBI sweep =====
  bool _kl15On=false; uint8_t _lastB0=0xFF;
  bool _sweepEnabled=true, _sweepAcceptACC=false, _sweepArmed=false;
  uint32_t _armTime=0;
  uint16_t _targetKmh=260, _targetRpm=5500;
  uint16_t _spdDelay=28, _rpmDelay=35, _peakDwell=1000, _startAfterKL15=4000;
  enum SweepState : uint8_t { SW_IDLE, SW_WAIT_DELAY, SW_TO_MAX_2, SW_PEAK, SW_TO_STOP_1, SW_TO_STOP_2 };
  SweepState _swState = SW_IDLE;
  uint32_t _swDeadline=0;
  void sendKombiPL(const uint8_t* pl, uint8_t len);
  void sendBurst(const uint8_t* pl, uint8_t len);
  void spdStop(); void tacStop(); void spdMax(); void tacMax();
  uint16_t kmh_to_raw(uint16_t kmh); uint16_t rpm_to_raw(uint16_t rpm);
  void updateKL15_fromB0(uint8_t b0);

  // ===== helpers =====
  static inline bool Bit(const uint8_t *b, uint8_t idx, uint8_t mask){ return (idx<8) ? ((b[idx]&mask)!=0) : false; }

  // ===== DOORS =====
  DoorSnapshot _door = {false,false,false,false,false,false};
  static const uint8_t DOOR_Q_CAP = 8;
  DoorEvent _doorQ[DOOR_Q_CAP]; uint8_t _dqHead=0, _dqTail=0;
  void pushDoorEvent(DoorEventType t, uint32_t now);
  void handleDoors_2FC(const uint8_t *buf, uint8_t len, uint32_t now);

  // ===== HANDBRAKE =====
  bool _handbrakeEngaged = true; // default safe
  static const uint8_t HB_Q_CAP = 4;
  HandbrakeEvent _hbQ[HB_Q_CAP]; uint8_t _hbHead=0, _hbTail=0;
  void pushHbEvent(HandbrakeEventType t, uint32_t now);
  void handleHandbrake_1B4(const uint8_t *buf, uint8_t len, uint32_t now);

  // ===== KEY / LOCK =====
  static const uint8_t KEY_Q_CAP = 6;
  KeyEvent _keyQ[KEY_Q_CAP]; uint8_t _keyHead=0, _keyTail=0;
  uint16_t _keyCooldownMs = 250;   // avoid multi-events per press
  uint32_t _lastKeyMs = 0;
  uint8_t  _lastKeyRaw = 0xFF;

  KeySnapshot _keySnap = {0xFF, LockState::Unknown, 0};

  void pushKeyEvent(KeyEventType t, uint32_t now);
  void handleKey_23A(const uint8_t *buf, uint8_t len, uint32_t now);
  void  handleVoltage_3B4(const uint8_t* buf, uint8_t len);
  float _lastVoltage = NAN;
  bool  _voltageSeen = false;
};
