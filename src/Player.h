#pragma once
#include <Arduino.h>
#include <SoftwareSerial.h>
#include "DFRobotDFPlayerMini.h"
#include "Pins.h"

class Player {
public:
  static const uint8_t  DF_VOLUME = 24;
  static const uint16_t DF_WAKE_MS = 100;
  static const uint16_t AMP_ON_AFTER_BUSY_MS = 60;
  static const uint16_t AMP_PRE_OFF_MS = 80;

  Player();
  void begin();
  void setBenchMode(bool bench);
  bool playTrack(uint16_t track);
  void stop();
  void loop();
  bool isPlaying() const { return _playing; }
  uint16_t currentTrack() const { return _currentTrack; }

private:
  bool ensureReady();
  bool waitBusyLevel(int level, uint16_t timeout_ms);
  void dfDrain(uint16_t ms);

  SoftwareSerial _ss;
  DFRobotDFPlayerMini _df;

  bool _bench=false;
  bool _dfPowered=false;
  bool _relayOn=false;
  bool _playing=false;
  uint16_t _currentTrack=0;
};
