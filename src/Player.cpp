#include "Player.h"

static inline bool elapsedSince(uint32_t start_ms, uint32_t ms){
  return (uint32_t)(millis() - start_ms) >= ms;
}

Player::Player() : _ss(PIN_DF_RX, PIN_DF_TX) {}

void Player::begin() {
  pinMode(PIN_DF_EN, OUTPUT);
  pinMode(PIN_SPK_RELAY, OUTPUT);
  pinMode(PIN_DF_BUSY, INPUT_PULLUP);
  digitalWrite(PIN_SPK_RELAY, LOW);
  digitalWrite(PIN_DF_EN, LOW);
  _relayOn = false;
  _dfPowered = false;
  _lastActiveMs = millis();
}

void Player::setBenchMode(bool bench) { _bench = bench; }

void Player::setVolume(uint8_t v) {
  if (v > DF_VOLUME_MAX) v = DF_VOLUME_MAX;
  _volume = v;
  if (_dfPowered) {
    _df.setVolume(_volume);
    dfDrain(10);
  }
}

bool Player::ensureReady() {
  if (!_dfPowered) {
    digitalWrite(PIN_DF_EN, HIGH);
    _dfPowered = true;
    delay(DF_WAKE_MS);
    _lastActiveMs = millis();
  }

  _ss.begin(9600);
  delay(10);
  _df.begin(_ss, PIN_DF_BUSY, true);
  _df.setVolume(_volume);
  dfDrain(50);
  return true;
}

bool Player::waitBusyLevel(int level, uint16_t timeout_ms) {
  const uint32_t t0 = millis();
  while (!elapsedSince(t0, timeout_ms)) {
    if (digitalRead(PIN_DF_BUSY) == level) return true;
    delay(2);
  }
  return false;
}

void Player::dfDrain(uint16_t ms) {
  const uint32_t t0 = millis();
  while (!elapsedSince(t0, ms)) {
    _df.update();
    while (_df.available()) (void)_df.readEvent();
    delay(2);
  }
}

bool Player::playTrack(uint16_t track) {
  if (track < 1) track = 1;
  if (track > DF_MAX_MP3) track = DF_MAX_MP3;

  digitalWrite(PIN_SPK_RELAY, LOW);
  _relayOn = false;

  if (!ensureReady()) return false;

  _df.send(0x12, track, false);
  dfDrain(8);

  if (!waitBusyLevel(LOW, 100)) {
    _df.send(0x16, 0, false);
    dfDrain(20);
    _df.send(0x12, track, false);
    dfDrain(8);
    if (!waitBusyLevel(LOW, 100)) return false;
  }

  delay(AMP_ON_AFTER_BUSY_MS);
  digitalWrite(PIN_SPK_RELAY, HIGH);
  _relayOn = true;

  _playing = true;
  _currentTrack = track;
  _lastActiveMs = millis();
  return true;
}

void Player::stop(bool forcePowerOff) {
  if (_relayOn) {
    delay(AMP_PRE_OFF_MS);
    digitalWrite(PIN_SPK_RELAY, LOW);
    _relayOn = false;
  }

  _df.send(0x16, 0, false);
  dfDrain(20);
  (void)waitBusyLevel(HIGH, 400);

  _playing = false;
  _currentTrack = 0;
  _lastActiveMs = millis();

  if (forcePowerOff || _bench) return;
  // allow auto-sleep to handle power down later
}

void Player::maybeAutoSleep() {
  if (_bench) return;  // no autosleep in bench mode
  if (!_dfPowered) return; // already off
  if (_playing) {
    _lastActiveMs = millis();
    return;
  }

  if (elapsedSince(_lastActiveMs, DF_AUTOSLEEP_DELAY_MS)) {
    digitalWrite(PIN_DF_EN, LOW);
    _dfPowered = false;
    _relayOn = false;
  }
}

void Player::loop() {
  // detect playback completion
  if (_playing && digitalRead(PIN_DF_BUSY) == HIGH) {
    stop(false);
  }

  maybeAutoSleep();
}
