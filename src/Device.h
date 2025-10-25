#pragma once
#include <Arduino.h>
#include <math.h>
#include "Pins.h"
#include "Player.h"
#include "Filter.h"

class Device {
public:
  Device() = default;

  // Boot-time init
  void begin();

  // Main loop
  void loop();

private:
  // ======= Config / constants =======
  static constexpr float    RADIO_MIN_VOLT    = 11.8f;
  static constexpr bool     FAILSAFE_NO_RADIO = true;

  // ======= Helpers =======
  static inline void DBG(const __FlashStringHelper* s){
#if 1
    Serial.println(s);
#endif
  }
  void parseCLI();
  void radioSet(bool on);
  void playWelcome();
  void playTrackNow(uint16_t tr);
  void ensureSeatbeltLoop();
  void stopIfTrack(uint16_t tr);
  bool  batteryOK();

  // ======= Tiny deferral queue (for notifications blocked by Welcome) =======
  struct QItem { uint16_t track; };
  static constexpr uint8_t QCAP = 8;
  QItem q[QCAP]; uint8_t qw=0, qr=0;
  bool qPush(uint16_t tr){ uint8_t n=(uint8_t)(qw+1u)%QCAP; if(n==qr) return false; q[qw]={tr}; qw=n; return true; }
  bool qPop(uint16_t& tr){ if(qr==qw) return false; tr=q[qr].track; qr=(uint8_t)(qr+1u)%QCAP; return true; }

  // ======= Members =======
  enum class NowPlaying : uint8_t { None, Welcome, Other };
  NowPlaying nowPlaying = NowPlaying::None;

  Filter filter;      // CAN ingest + KOMBI + state + play-intents
  Player player;

  // Radio hold after KL15 OFF (policy choice)
  bool kl15Prev = false;
  bool radioHeldAfterIgnOff = false;
};
