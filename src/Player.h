#pragma once
#include <Arduino.h>
#include <SoftwareSerial.h>
#include "DFPMini.h"
#include "Pins.h"
#include "CCIDMap.h"

class Player {
public:
  static const uint8_t  DF_VOLUME_DEFAULT = 12;
  static const uint8_t  DF_VOLUME_MAX     = 30;
  static const uint16_t DF_WAKE_MS = 100;
  static const uint16_t AMP_ON_AFTER_BUSY_MS = 60;
  static const uint16_t AMP_PRE_OFF_MS = 80;
  static const uint32_t DF_AUTOSLEEP_DELAY_MS = 10000; // 10s idle → sleep

  Player();
  void begin();
  void setBenchMode(bool bench);

  bool playTrack(uint16_t track);   // 1..DF_MAX_MP3
  void stop(bool forcePowerOff = false);
  void loop();
  bool isPlaying() const { return _playing; }
  uint16_t currentTrack() const { return _currentTrack; }

  // Volume control
  void setVolume(uint8_t v);
  uint8_t volume() const { return _volume; }

private:
  bool ensureReady();
  bool waitBusyLevel(int level, uint16_t timeout_ms);
  void dfDrain(uint16_t ms);
  void maybeAutoSleep();   // <— new helper

  SoftwareSerial _ss;
  DFPMini _df;

  bool _bench = false;
  bool _dfPowered = false;
  bool _relayOn = false;
  bool _playing = false;
  uint16_t _currentTrack = 0;

  uint8_t _volume = DF_VOLUME_DEFAULT;
  uint32_t _lastActiveMs = 0;   // last time we were playing or powered
};
