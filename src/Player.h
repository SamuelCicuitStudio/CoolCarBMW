#pragma once
#include <Arduino.h>
#include <SoftwareSerial.h>
#include "DFPMini.h"        // swapped in: rely on your minimal DFPlayer driver
#include "Pins.h"
#include "CCIDMap.h"

class Player {
public:
  static const uint8_t  DF_VOLUME_DEFAULT = 12;
  static const uint8_t  DF_VOLUME_MAX     = 30;
  static const uint16_t DF_WAKE_MS = 100;
  static const uint16_t AMP_ON_AFTER_BUSY_MS = 60;
  static const uint16_t AMP_PRE_OFF_MS = 80;

  Player();
  void begin();
  void setBenchMode(bool bench);

  bool playTrack(uint16_t track); // 1..DF_MAX_MP3
  void stop();
  void loop();
  bool isPlaying() const { return _playing; }
  uint16_t currentTrack() const { return _currentTrack; }

  // Volume control (0..30)
  void setVolume(uint8_t v);
  uint8_t volume() const { return _volume; }

private:
  bool ensureReady();
  bool waitBusyLevel(int level, uint16_t timeout_ms);
  void dfDrain(uint16_t ms);

  SoftwareSerial _ss;
  DFPMini _df;               // <— now using your DFPMini backend

  bool _bench=false;
  bool _dfPowered=false;
  bool _relayOn=false;
  bool _playing=false;
  uint16_t _currentTrack=0;

  uint8_t _volume = DF_VOLUME_DEFAULT;
};
