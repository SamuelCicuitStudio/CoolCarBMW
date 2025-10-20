#pragma once
#include <Arduino.h>
#include <SoftwareSerial.h>
#include "DFPMini.h"
#include "Pins.h"
#include "CCIDMap.h"

class Player {
public:
  // ---- Tunables (kept conservative for reliable wake) ----
  static const uint8_t  DF_VOLUME_DEFAULT = 12;    // 0..30
  static const uint8_t  DF_VOLUME_MAX     = 30;

  // Power-up settle before talking to DF mini after EN=HIGH
  static const uint16_t DF_WAKE_MS        = 350;

  // Extra settle after sending RESET (internal init / media scan)
  static const uint16_t DF_POST_RESET_MS  = 400;

  // Total time to wait for DF to report "idle/ready" (BUSY=HIGH) after boot
  static const uint16_t DF_READY_TIMEOUT_MS = 1500;

  // Relay timing
  static const uint16_t AMP_ON_AFTER_BUSY_MS = 60;   // delay after BUSY goes LOW (playing) before relay ON
  static const uint16_t AMP_PRE_OFF_MS       = 80;   // small delay before relay OFF to avoid click

  // Idle autosleep (power cut) when not playing
  static const uint32_t DF_AUTOSLEEP_DELAY_MS = 10000; // 10s

  Player();

  // lifecycle
  void begin();
  void setBenchMode(bool bench) { _bench = bench; }

  // playback
  bool playTrack(uint16_t track);                 // 1..DF_MAX_MP3
  bool playCCID(uint16_t ccid) { return playTrack(trackForCcid(ccid)); }
  void stop(bool forcePowerOff = false);
  void pause();

  // loop
  void loop();

  // volume
  void setVolume(uint8_t v);
  uint8_t volume() const { return _volume; }

  // state
  bool isPlaying() const { return _playing; }
  bool isAwake()   const { return _dfPowered; }
  uint16_t currentTrack() const { return _currentTrack; }

private:
  // readiness & power
  bool ensureReady();                               // fully (re)connect & wait for DF to be ready (and set volume)
  void powerOnDF();
  void powerOffDF();

  // relay control
  void relayOn();
  void relayOff();

  // waits & helpers
  bool waitBusyLevel(int level, uint16_t timeout_ms);
  bool waitForDFReady(uint16_t timeout_ms);         // BUSY==HIGH and/or init events
  void pumpDF(uint16_t ms);                         // parse/flush inbound frames while waiting
  void maybeAutoSleep();
  static inline bool elapsedSince(uint32_t start_ms, uint32_t ms);

  SoftwareSerial _ss;
  DFPMini _df;

  bool _bench = false;
  bool _dfPowered = false;
  bool _relayOn = false;
  bool _playing = false;

  uint16_t _currentTrack = 0;
  uint8_t _volume = DF_VOLUME_DEFAULT;
  uint32_t _lastActiveMs = 0;
};
