#include "Player.h"

static inline bool elapsedSince(uint32_t start_ms, uint32_t ms){
  return (uint32_t)(millis() - start_ms) >= ms;
}

Player::Player() : _ss(PIN_DF_RX, PIN_DF_TX) {}

void Player::begin(){
  pinMode(PIN_DF_EN, OUTPUT);
  pinMode(PIN_SPK_RELAY, OUTPUT);
  pinMode(PIN_DF_BUSY, INPUT_PULLUP);
  digitalWrite(PIN_SPK_RELAY, LOW);
  _relayOn = false;

  // Keep the DF powered down until first play (same behavior)
  digitalWrite(PIN_DF_EN, LOW);
  _dfPowered = false;
}

void Player::setBenchMode(bool bench){ _bench = bench; }

void Player::setVolume(uint8_t v){
  if(v > DF_VOLUME_MAX) v = DF_VOLUME_MAX;
  _volume = v;
  if(_dfPowered){
    _df.setVolume(_volume);   // DFPMini API
    dfDrain(10);
  }
}

bool Player::ensureReady(){
  // Speaker relay off before we touch the module (same behavior)
  digitalWrite(PIN_SPK_RELAY, LOW);
  _relayOn = false;

  if(!_dfPowered){
    digitalWrite(PIN_DF_EN, HIGH);
    _dfPowered = true;
    delay(DF_WAKE_MS);
  }

  // Serial link up + DFPMini begin (hands it the BUSY pin + active-LOW)
  _ss.begin(9600);
  delay(10);
  _df.begin(_ss, PIN_DF_BUSY, /*busyActiveLow=*/true);

  // Default volume (like before)
  _df.setVolume(_volume);
  dfDrain(50);
  return true;  // DFPMini::begin() is non-failing; keep behavior flow same
}

bool Player::waitBusyLevel(int level, uint16_t timeout_ms){
  const uint32_t t0 = millis();
  while(!elapsedSince(t0, timeout_ms)){
    if(digitalRead(PIN_DF_BUSY) == level) return true;
    delay(2);
  }
  return false;
}

void Player::dfDrain(uint16_t ms){
  const uint32_t t0 = millis();
  while(!elapsedSince(t0, ms)){
    _df.update();                   // parse inbound frames
    while(_df.available()) { (void)_df.readEvent(); }  // drain event queue
    delay(2);
  }
}

bool Player::playTrack(uint16_t track){
  if(track < 1) track = 1;
  if(track > DF_MAX_MP3) track = DF_MAX_MP3;

  // Speaker relay off before starting a new track
  digitalWrite(PIN_SPK_RELAY, LOW);
  _relayOn = false;

  if(!ensureReady()) return false;

  // Emulate DFRobot's playMp3Folder(track) using DF command 0x12
  _df.send(0x12, track, /*feedback=*/false);
  dfDrain(8);

  // Wait for BUSY to go LOW (playing). Retry once, like original.
  if(!waitBusyLevel(LOW, 100)){
    _df.send(0x16, 0, false);   // STOP (cmd 0x16)
    dfDrain(20);
    _df.send(0x12, track, false);
    dfDrain(8);
    if(!waitBusyLevel(LOW, 100)) return false;
  }

  // Turn amp on slightly after BUSY asserted (same timing)
  delay(AMP_ON_AFTER_BUSY_MS);
  digitalWrite(PIN_SPK_RELAY, HIGH);
  _relayOn = true;

  _playing = true;
  _currentTrack = track;
  return true;
}

void Player::stop(){
  if(_relayOn){
    delay(AMP_PRE_OFF_MS);
    digitalWrite(PIN_SPK_RELAY, LOW);
    _relayOn = false;
  }

  // STOP via DFPMini raw send (0x16)
  _df.send(0x16, 0, false);
  dfDrain(20);

  // Wait for BUSY to release (HIGH)
  (void)waitBusyLevel(HIGH, 400);

  // Power-down module in non-bench mode (same behavior)
  if(!_bench){
    digitalWrite(PIN_DF_EN, LOW);
    _dfPowered = false;
  }

  _playing = false;
  _currentTrack = 0;
}

void Player::loop(){
  // Auto-stop when module reports idle via BUSY pin
  if(_playing && digitalRead(PIN_DF_BUSY) == HIGH){
    stop();
  }
}
