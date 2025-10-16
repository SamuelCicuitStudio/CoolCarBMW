/*
  DFPMini — Nano minimal control: PLAY any track, set VOLUME, show STATUS
  - EN (D7) held HIGH all the time
  - AMP_EN (D6) mirrors BUSY (HIGH while playing)
  - NO auto-play, NO auto-advance
  - Uses CMD 0x12 so "46" plays /MP3/0046.mp3 exactly

  Pins (Arduino Nano 5V):
    D3  -> DF RX   (add ~1k series resistor to protect DF RX)
    D4  <- DF TX   (DF TX ≈3.3V; readable on Nano)
    D5  <- BUSY    (LOW while playing on most boards; busyActiveLow=true)
    D7  -> DF EN   (held HIGH permanently)
    D6  -> AMP EN  (HIGH while playing)

  Files:
    /MP3/0001.mp3 .. /MP3/3000.mp3

  Serial Monitor: 115200 (commands below), DFPlayer: 9600 8N1

  Commands:
    N           -> play /MP3/00NN.mp3 (e.g., 7 or 46)
    play N      -> same as above
    vNN         -> set volume 0..30  (e.g., v20)
    vol NN      -> same as above
    s           -> status line
    status      -> same as above
*/

#include <Arduino.h>
#include <SoftwareSerial.h>
#include <string.h>   // strcmp, strchr
#include <ctype.h>    // tolower
#include "DFPMini.h"

// ----- Pins -----
const uint8_t PIN_DF_RX  = 4;   // Arduino RX  <- DF TX
const uint8_t PIN_DF_TX  = 3;   // Arduino TX  -> DF RX (series ~1k)
const uint8_t PIN_BUSY   = 5;   // LOW while playing (typical)
const uint8_t PIN_DF_EN  = 7;   // kept HIGH always
const uint8_t PIN_AMP_EN = 6;   // HIGH while playing

// ----- Serial + driver -----
SoftwareSerial DFPSER(PIN_DF_RX, PIN_DF_TX); // RX, TX
DFPMini dfp;

// ----- State -----
static uint8_t  gVolume = 16;  // 0..30
static uint16_t gIndex  = 1;   // /MP3/0001.mp3

// ----- Helpers -----
static void setAmp(bool on) { digitalWrite(PIN_AMP_EN, on ? HIGH : LOW); }

static void printPaddedIndex(uint16_t idx) {
  if (idx < 10)        Serial.print(F("000"));
  else if (idx < 100)  Serial.print(F("00"));
  else if (idx < 1000) Serial.print(F("0"));
  Serial.print(idx);
}

// Play /MP3/00NNN.mp3 using CMD 0x12 (correct mapping to MP3 folder)
static void playMp3FolderIndex(uint16_t idx) {
  if (idx < 1) idx = 1;
  if (idx > 3000) idx = 3000;
  gIndex = idx;
  dfp.send(0x12, gIndex, /*feedback=*/false); // CMD 0x12: play MP3 folder index
  Serial.print(F("Playing MP3/")); printPaddedIndex(gIndex); Serial.println(F(".mp3"));
  Serial.print(F("> "));
}

static void setVol(uint8_t v) {
  if (v > 30) v = 30;
  gVolume = v;
  dfp.setVolume(gVolume);
  Serial.print(F("Volume=")); Serial.println(gVolume);
  Serial.print(F("> "));
}

static void printStatus() {
  bool playing = dfp.isPlayingBusyPin();
  Serial.print(F("Status: "));
  Serial.print(playing ? F("PLAYING") : F("NOT PLAYING"));
  Serial.print(F(", volume=")); Serial.print(gVolume);
  Serial.print(F(", index="));  Serial.println(gIndex);
  Serial.print(F("> "));
}

static uint16_t parseUInt(const char* s) {
  uint32_t x = 0; bool any = false;
  for (const char* p = s; *p; ++p) {
    if (*p >= '0' && *p <= '9') { x = x*10 + (*p - '0'); any = true; }
    else if (*p==' ' || *p=='\t') continue;
    else return 0xFFFF;
  }
  if (!any) return 0xFFFF;
  if (x > 65535) x = 65535;
  return (uint16_t)x;
}

// ----- Command parser (minimal) -----
static void handleLine(char* line) {
  // lowercase + trim
  for (char* p=line; *p; ++p) *p = (char)tolower(*p);
  char* s = line; while (*s==' '||*s=='\t') ++s;
  if (!*s) { Serial.print(F("> ")); return; }

  // Try pure number -> play that MP3 index
  uint16_t asNum = parseUInt(s);
  if (asNum != 0xFFFF) { playMp3FolderIndex(asNum); return; }

  // Tokenize: cmd [arg]
  char* cmd = s;
  char* arg = strchr(s, ' ');
  if (arg) { *arg++ = 0; while (*arg==' '||*arg=='\t') ++arg; }

  if (!strcmp(cmd,"play")) {
    uint16_t n = arg ? parseUInt(arg) : 0xFFFF;
    if (n == 0xFFFF) { Serial.println(F("Usage: play <index>")); Serial.print(F("> ")); }
    else playMp3FolderIndex(n);

  } else if (!strcmp(cmd,"v") || !strcmp(cmd,"vol")) {
    uint16_t n = arg ? parseUInt(arg) : 0xFFFF;
    if (n == 0xFFFF || n > 30) { Serial.println(F("Usage: v<0..30>  or  vol <0..30>")); Serial.print(F("> ")); }
    else setVol((uint8_t)n);

  } else if (!strcmp(cmd,"s") || !strcmp(cmd,"status")) {
    printStatus();

  } else {
    Serial.println(F("Commands: <N>, play N, vNN, vol NN, s/status"));
    Serial.print(F("> "));
  }
}

// ----- Arduino lifecycle -----
void setup() {
  pinMode(PIN_BUSY,   INPUT);
  pinMode(PIN_DF_EN,  OUTPUT);
  pinMode(PIN_AMP_EN, OUTPUT);

  // EN always HIGH; AMP off initially
  digitalWrite(PIN_DF_EN,  HIGH);
  digitalWrite(PIN_AMP_EN, LOW);

  Serial.begin(115200);
  DFPSER.begin(9600);

  // DFPlayer boot time (filesystem scan)
  delay(2000);

  dfp.begin(DFPSER, PIN_BUSY, /*busyActiveLow=*/true);
  dfp.setSource(DFPMini::SRC_TF);
  delay(200);
  dfp.setEQ(DFPMini::EQ_NORMAL);
  dfp.setVolume(gVolume);

  Serial.println(F("DFPlayer ready. Minimal commands: <N>, play N, vNN, vol NN, s/status"));
  Serial.print(F("> "));
}

void loop() {
  dfp.update();

  // Mirror BUSY -> AMP_EN
  static bool lastPlaying = false;
  bool playing = dfp.isPlayingBusyPin();
  if (playing != lastPlaying) {
    lastPlaying = playing;
    setAmp(playing);
  }

  // No event-based auto-advance

  // Line input
  static char buf[48];
  static uint8_t len = 0;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r' || c == '\n') {
      if (len) { buf[len] = 0; handleLine(buf); len = 0; }
    } else {
      if (len < sizeof(buf)-1) buf[len++] = c;
    }
  }
}
