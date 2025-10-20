#include "Player.h"

// ---- Small utility ----
inline bool Player::elapsedSince(uint32_t start_ms, uint32_t ms) {
  return (uint32_t)(millis() - start_ms) >= ms;
}

Player::Player() : _ss(PIN_DF_RX, PIN_DF_TX) {}

void Player::begin() {
  pinMode(PIN_DF_EN, OUTPUT);
  pinMode(PIN_SPK_RELAY, OUTPUT);
  pinMode(PIN_DF_BUSY, INPUT_PULLUP);

  // Safe idle states
  digitalWrite(PIN_SPK_RELAY, LOW);  _relayOn   = false;
  digitalWrite(PIN_DF_EN, LOW);      _dfPowered = false;

  _playing = false;
  _currentTrack = 0;
  _lastActiveMs = millis();

  // We (re)start serial in ensureReady()
  _ss.end();
}

// ----- Volume -----
void Player::setVolume(uint8_t v) {
  if (v > DF_VOLUME_MAX) v = DF_VOLUME_MAX;
  _volume = v;

  // If awake, apply immediately; otherwise it will be applied on next ensureReady()
  if (_dfPowered) {
    _df.setVolume(_volume);
    pumpDF(50);
    _lastActiveMs = millis();
  }
}

// ----- Power / readiness sequence -----
void Player::powerOnDF() {
  digitalWrite(PIN_DF_EN, HIGH);
  _dfPowered = true;
  delay(DF_WAKE_MS);            // let power rails & DF core stabilize
  _lastActiveMs = millis();
}

void Player::powerOffDF() {
  if (!_dfPowered) return;

  // Always open relay first to avoid pop
  delay(AMP_PRE_OFF_MS);
  relayOff();

  digitalWrite(PIN_DF_EN, LOW);
  _dfPowered = false;
}

bool Player::ensureReady() {
  bool coldBoot = false;

  if (!_dfPowered) {
    // We are coming from sleep → reconnect fully
    powerOnDF();
    coldBoot = true;
  }

  // Hard (re)start serial each time to avoid stale state after sleep
  _ss.end();
  _ss.begin(9600);
  delay(10);

  // (Re)initialise DFPMini driver against our serial and BUSY pin
  _df.begin(_ss, PIN_DF_BUSY, true); // BUSY active LOW on typical DF mini

  if (coldBoot) {
    // Send RESET right after power-up; let it internally reinit & scan media
    _df.reset(false);
    pumpDF(DF_POST_RESET_MS);

    // Optional: ensure source is TF (most boards default to TF anyway)
    _df.setSource(DFPMini::SRC_TF, false);
    pumpDF(50);

    // Wait until DF reports "idle/ready": BUSY must be HIGH (not playing)
    if (!waitForDFReady(DF_READY_TIMEOUT_MS)) {
      // If the module isn't ready, there's no point in continuing
      return false;
    }
  } else {
    // If not a cold boot, still ensure we're idle before issuing new commands
    (void)waitBusyLevel(HIGH, 500);
  }

  // Apply volume (again) after any reset or reattach
  _df.setVolume(_volume);
  pumpDF(50);

  return true;
}

// Wait for BUSY to a specific logic level
bool Player::waitBusyLevel(int level, uint16_t timeout_ms) {
  const uint32_t t0 = millis();
  while (!elapsedSince(t0, timeout_ms)) {
    if (digitalRead(PIN_DF_BUSY) == level) return true;
    pumpDF(2);
  }
  return (digitalRead(PIN_DF_BUSY) == level);
}

// Wait for the DF mini to be "ready": BUSY HIGH and/or init/device-in events observed
bool Player::waitForDFReady(uint16_t timeout_ms) {
  const uint32_t t0 = millis();
  bool sawInitOrDevice = false;

  while (!elapsedSince(t0, timeout_ms)) {
    // BUSY HIGH means idle/ready (per wiring comment)
    if (digitalRead(PIN_DF_BUSY) == HIGH) return true;

    // Parse inbound frames and look for EV_INIT/EV_DEVICE_IN
    _df.update();
    while (_df.available()) {
      auto ev = _df.readEvent();
      if (ev.type == DFPMini::EV_INIT || ev.type == DFPMini::EV_DEVICE_IN) {
        sawInitOrDevice = true;
      }
    }
    delay(2);
  }

  // If we saw init/device but BUSY never turned HIGH, try one last BUSY read
  if (sawInitOrDevice) return (digitalRead(PIN_DF_BUSY) == HIGH);
  return false;
}

// Parse inbound frames for a bit while waiting (keeps command pipeline healthy)
void Player::pumpDF(uint16_t ms) {
  const uint32_t t0 = millis();
  while (!elapsedSince(t0, ms)) {
    _df.update();
    while (_df.available()) (void)_df.readEvent();
    delay(2);
  }
}

// ----- Playback -----
bool Player::playTrack(uint16_t track) {
  if (track < 1) track = 1;
  if (track > DF_MAX_MP3) track = DF_MAX_MP3;

  // Ensure path is quiet before starting anything new
  relayOff();

  if (!ensureReady()) return false;

  // Double-check idle right before PLAY (protect against lingering activity)
  (void)waitBusyLevel(HIGH, 500);

  // PLAY by index (0x12) – we rely on simple 1..N mapping
  _df.send(0x12, track, false);
  pumpDF(8);

  // Confirm BUSY goes LOW (playing). If not, attempt one clean retry.
  if (!waitBusyLevel(LOW, 900)) {
    _df.send(0x16, 0, false);   // STOP
    pumpDF(60);
    (void)waitBusyLevel(HIGH, 500);

    _df.send(0x12, track, false);
    pumpDF(8);
    if (!waitBusyLevel(LOW, 900)) {
      // Still not playing — bail out without engaging the relay
      _playing = false;
      _currentTrack = 0;
      return false;
    }
  }

  // Now actually playing → engage relay slightly after BUSY transitions
  delay(AMP_ON_AFTER_BUSY_MS);
  relayOn();

  _playing = true;
  _currentTrack = track;
  _lastActiveMs = millis();
  return true;
}

void Player::pause() {
  if (!_dfPowered) return;
  _df.pause(false);
  pumpDF(8);
  _playing = false;
  _lastActiveMs = millis();

  // Give amp a moment before opening relay (reduces click)
  delay(AMP_PRE_OFF_MS);
  relayOff();
}

void Player::stop(bool forcePowerOff) {
  if (_dfPowered) {
    _df.send(0x16, 0, false);   // STOP
    pumpDF(20);
    (void)waitBusyLevel(HIGH, 500);
  }

  _playing = false;
  _currentTrack = 0;
  _lastActiveMs = millis();

  delay(AMP_PRE_OFF_MS);
  relayOff();

  if (forcePowerOff) powerOffDF();
}

// ----- Relay helpers -----
void Player::relayOn() {
  if (_relayOn) return;
  digitalWrite(PIN_SPK_RELAY, HIGH);
  _relayOn = true;
}

void Player::relayOff() {
  if (!_relayOn) { digitalWrite(PIN_SPK_RELAY, LOW); return; }
  digitalWrite(PIN_SPK_RELAY, LOW);
  _relayOn = false;
}

// ----- Autosleep & loop -----
void Player::maybeAutoSleep() {
  if (_bench) return;        // never autosleep in bench mode
  if (!_dfPowered) return;   // already off
  if (_playing) {
    _lastActiveMs = millis();
    return;
  }
  if (elapsedSince(_lastActiveMs, DF_AUTOSLEEP_DELAY_MS)) {
    // Clean shutdown path: drop relay first, then DF power
    relayOff();
    powerOffDF();
  }
}

void Player::loop() {
  // Parse any DF inbound responses so our small queues never clog
  _df.update();
  while (_df.available()) (void)_df.readEvent();

  // Detect playback completion via BUSY (HIGH = idle)
  if (_playing && digitalRead(PIN_DF_BUSY) == HIGH) {
    // Logical stop; no need to power off immediately
    stop(false);
  }

  maybeAutoSleep();
}
