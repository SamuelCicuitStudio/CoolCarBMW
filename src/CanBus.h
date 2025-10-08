#pragma once
#include <Arduino.h>
#include <SPI.h>
#include "mcp_can.h"
#include "Pins.h"

class CanBus {
public:
  explicit CanBus(uint8_t csPin = PIN_CAN_CS);

  bool begin();

  // returns next *distinct* frame (deduplicated using history + time window)
  bool readOnceDistinct(uint32_t &id, uint8_t &len, uint8_t *buf);

  // feed frames (so we can watch KL15 0x130) and drive sweep state
  void onFrame(uint32_t id, uint8_t len, const uint8_t *buf);
  void tickSweep();

  // sweep configuration
  void enableSweep(bool on) { _sweepEnabled = on; }
  void setSweepAcceptACC(bool on) { _sweepAcceptACC = on; }
  void setSweepTargets(uint16_t kmh, uint16_t rpm){ _targetKmh = kmh; _targetRpm = rpm; }
  void setSweepTiming(uint16_t speedDelayMs, uint16_t rpmDelayMs, uint16_t peakDwellMs, uint16_t startAfterMs){
    _spdDelay = speedDelayMs; _rpmDelay = rpmDelayMs; _peakDwell = peakDwellMs; _startAfterKL15 = startAfterMs;
  }

  void setDedupWindow(uint16_t ms) { _dedupWindowMs = ms; }
  void setHistoryDepth(uint8_t depth) { _historyDepth = (depth>10)?10:depth; }

private:
  // ====== duplicate filter ======
  struct FrameRec {
    uint32_t id;
    uint8_t  len;
    uint8_t  data[8];
    uint32_t t;
    bool     valid;
  };

  MCP_CAN _can;
  static const uint8_t MAX_HISTORY = 10;
  FrameRec _hist[MAX_HISTORY];
  uint8_t  _histHead;
  uint8_t  _historyDepth;
  uint16_t _dedupWindowMs;

  bool readRaw(uint32_t &id, uint8_t &len, uint8_t *buf);
  bool isDuplicate(uint32_t id, uint8_t len, const uint8_t *buf, uint32_t now) const;
  void pushHistory(uint32_t id, uint8_t len, const uint8_t *buf, uint32_t now);

  // ====== KL15 + sweep ======
  bool _kl15On=false;
  uint8_t _lastB0=0xFF;
  bool _sweepEnabled=true;
  bool _sweepAcceptACC=false;
  bool _sweepArmed=false;
  uint32_t _armTime=0;

  uint16_t _targetKmh=260;  // defaults from your sample
  uint16_t _targetRpm=5500;
  uint16_t _spdDelay=28;
  uint16_t _rpmDelay=35;
  uint16_t _peakDwell=1000;
  uint16_t _startAfterKL15=4000;

  // non-blocking phase machine
  enum SweepState : uint8_t { SW_IDLE, SW_WAIT_DELAY, SW_TO_MAX_1, SW_TO_MAX_2, SW_PEAK, SW_TO_STOP_1, SW_TO_STOP_2 };
  SweepState _swState = SW_IDLE;
  uint32_t _swDeadline=0;

  // helpers for KOMBI frames
  void sendKombiPL(const uint8_t* pl, uint8_t len);
  void sendBurst(const uint8_t* pl, uint8_t len);
  void spdStop();
  void tacStop();
  void spdMax();
  void tacMax();
  uint16_t kmh_to_raw(uint16_t kmh);
  uint16_t rpm_to_raw(uint16_t rpm);

  void updateKL15_fromB0(uint8_t b0);
};
