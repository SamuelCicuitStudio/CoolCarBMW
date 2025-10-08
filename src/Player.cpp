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

  if(_bench){
    digitalWrite(PIN_DF_EN, HIGH);
    delay(DF_WAKE_MS);
    _ss.begin(9600);
    _df.begin(_ss);
    _df.setTimeOut(500);
    _df.volume(DF_VOLUME);
    dfDrain(60);
    _dfPowered = true;
  } else {
    digitalWrite(PIN_DF_EN, LOW);
    _dfPowered = false;
  }
}

void Player::setBenchMode(bool bench){ _bench = bench; }

bool Player::ensureReady(){
  digitalWrite(PIN_SPK_RELAY, LOW);
  _relayOn = false;

  if(!_dfPowered){
    digitalWrite(PIN_DF_EN, HIGH);
    _dfPowered = true;
    delay(DF_WAKE_MS);
  }
  _ss.begin(9600);
  delay(10);
  if(!_df.begin(_ss)){
    digitalWrite(PIN_DF_EN, LOW); delay(300);
    digitalWrite(PIN_DF_EN, HIGH); delay(DF_WAKE_MS);
    _ss.begin(9600);
    if(!_df.begin(_ss)) return false;
  }
  _df.setTimeOut(500);
  _df.volume(DF_VOLUME);
  dfDrain(50);
  return true;
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
    if(_df.available()){ (void)_df.readType(); (void)_df.read(); }
    else delay(2);
  }
}

bool Player::playTrack(uint16_t track){
  if(track < 1) track = 1;
  if(track > 30) track = 30;

  digitalWrite(PIN_SPK_RELAY, LOW);
  _relayOn = false;

  if(!ensureReady()) return false;

  _df.playMp3Folder(track);
  dfDrain(8);
  if(!waitBusyLevel(LOW, 400)){
    _df.stop(); dfDrain(20);
    _df.playMp3Folder(track); dfDrain(8);
    if(!waitBusyLevel(LOW, 400)) return false;
  }
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
  _df.stop();
  dfDrain(20);
  (void)waitBusyLevel(HIGH, 400);
  if(!_bench){
    digitalWrite(PIN_DF_EN, LOW);
    _dfPowered = false;
  }
  _playing = false;
  _currentTrack = 0;
}

void Player::loop(){
  if(_playing && digitalRead(PIN_DF_BUSY) == HIGH){
    stop();
  }
}
