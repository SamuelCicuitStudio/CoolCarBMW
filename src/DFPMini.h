
// DFPMini — Minimal, robust DFPlayer Mini driver
// Optimized for low RAM usage, no dynamic allocation, and non‑blocking parsing.
//
// Supports: play by index, play by folder/file, next/prev, volume/EQ/mode,
// device select, pause/resume, reset/standby, repeat, and queries.
// Works with any Arduino Stream (HardwareSerial, SoftwareSerial).
//
// Protocol per "MP3‑TF‑16P / DFPlayer Mini" datasheet:
// Frame: 0x7E, 0xFF, 0x06, CMD, FEEDBACK(0/1), PARAM_H, PARAM_L, CHK_H, CHK_L, 0xEF
//
// (c) 2025 Mihai + ChatGPT. MIT License.

#ifndef DFPMINI_H
#define DFPMINI_H

#include <Arduino.h>

class DFPMini {
public:
  // ==== Public enums (per datasheet) ====
  enum EQ : uint8_t { EQ_NORMAL=0, EQ_POP=1, EQ_ROCK=2, EQ_JAZZ=3, EQ_CLASSIC=4, EQ_BASS=5 };
  enum PlayMode : uint8_t { MODE_REPEAT=0, MODE_FOLDER_REPEAT=1, MODE_SINGLE_REPEAT=2, MODE_RANDOM=3 };
  // CMD 0x09 source values:
  // U=1, TF=2, AUX=3, SLEEP=4, FLASH=5  (per common variants; TF and U are the usual targets)
  enum Source : uint8_t { SRC_U=1, SRC_TF=2, SRC_AUX=3, SRC_SLEEP=4, SRC_FLASH=5 };

  struct Frame {
    uint8_t  ver;     // should be 0xFF
    uint8_t  len;     // should be 0x06
    uint8_t  cmd;
    uint8_t  fb;
    uint8_t  pH;
    uint8_t  pL;
    uint8_t  cH;
    uint8_t  cL;
  };

  // High‑level events you can poll via available()/readEvent().
  enum EventType : uint8_t {
    EV_NONE=0,
    EV_ACK=0x41,             // reply/ack
    EV_ERROR=0x40,           // error (param codes 0x0000 busy, 0x0001 frame incomplete, 0x0002 checksum)
    EV_INIT=0x3F,            // initialization / online device bitmap
    EV_DEVICE_IN=0x3A,       // device inserted (param: 1 U‑disk, 2 TF)
    EV_DEVICE_OUT=0x3B,      // device removed  (param: 1 U‑disk, 2 TF)
    EV_UDISK_FIN=0x3C,       // U‑disk finished track N
    EV_TF_FIN=0x3D,          // TF finished track N
    EV_FLASH_FIN=0x3E,       // FLASH finished track N
    // Query responses (command echoed back as cmd)
    EV_STATUS=0x42, EV_VOL=0x43, EV_EQ=0x44, EV_MODE=0x45, EV_SWVER=0x46,
    EV_TF_TOTAL=0x47, EV_U_TOTAL=0x48, EV_FLASH_TOTAL=0x49, EV_KEEPON=0x4A,
    EV_TF_CUR=0x4B, EV_U_CUR=0x4C, EV_FLASH_CUR=0x4D
  };

  struct Event {
    EventType type;
    uint16_t  param;   // meaning depends on type
    uint8_t   raw[10]; // last full raw frame for debugging (0..9 filled)
  };

  DFPMini() : _serial(nullptr), _busyPin(0xFF), _busyActiveLow(true), _bufIndex(0), _queueHead(0), _queueTail(0) {}

  // Begin with any Stream (HardwareSerial, SoftwareSerial, etc.).
  void begin(Stream &s, uint8_t busyPin=0xFF, bool busyActiveLow=true) {
    _serial = &s;
    _busyPin = busyPin;
    _busyActiveLow = busyActiveLow;
    if (_busyPin != 0xFF) {
      pinMode(_busyPin, INPUT);
    }
    resetParser();
    clearEvents();
  }

  // ======== High‑level controls (non‑blocking) ========
  bool next(bool fb=false)                 { return send(0x01, 0, fb); }
  bool prev(bool fb=false)                 { return send(0x02, 0, fb); }
  bool playTrack(uint16_t n, bool fb=false){ return send(0x03, n, fb); } // 1..2999 recommended
  bool volumeUp(bool fb=false)             { return send(0x04, 0, fb); }
  bool volumeDown(bool fb=false)           { return send(0x05, 0, fb); }
  bool setVolume(uint8_t v, bool fb=false) { if (v>30) v=30; return send(0x06, v, fb); }
  bool setEQ(EQ eq, bool fb=false)         { if (eq>EQ_BASS) eq=EQ_BASS; return send(0x07, eq, fb); }
  bool setPlayMode(PlayMode m, bool fb=false){ if(m>MODE_RANDOM)m=MODE_RANDOM; return send(0x08, m, fb); }
  bool setSource(Source s, bool fb=false)  { if (s<1||s>5) s=SRC_TF; return send(0x09, s, fb); }
  bool standby(bool fb=false)              { return send(0x0A, 0, fb); }
  bool normal(bool fb=false)               { return send(0x0B, 0, fb); }
  bool reset(bool fb=false)                { return send(0x0C, 0, fb); }
  bool play(bool fb=false)                 { return send(0x0D, 0, fb); }
  bool pause(bool fb=false)                { return send(0x0E, 0, fb); }
  bool playFolderFile(uint8_t folder01to99, uint8_t file001to255, bool fb=false) {
    if (folder01to99<1) folder01to99=1;
    if (folder01to99>99) folder01to99=99;
    if (file001to255<1) file001to255=1;
    return send(0x0F, (uint16_t(folder01to99)<<8) | file001to255, fb);
  }
  // Volume adjust (gain) control (0..31), enable via DH=1
  bool setVolumeAdjust(bool enable, uint8_t gain=0, bool fb=false) {
    if (gain>31) gain=31;
    return send(0x10, (enable?0x0100:0x0000) | (gain & 0x1F), fb);
  }
  bool setRepeat(bool enable, bool fb=false){ return send(0x11, enable?1:0, fb); }

  // ======== Queries (responses arrive as events) ========
  bool queryStatus()       { return send(0x42, 0, true); } // playing/paused etc.
  bool queryVolume()       { return send(0x43, 0, true); }
  bool queryEQ()           { return send(0x44, 0, true); }
  bool queryPlayMode()     { return send(0x45, 0, true); }
  bool querySWVersion()    { return send(0x46, 0, true); }
  bool queryTFTotal()      { return send(0x47, 0, true); }
  bool queryUTotal()       { return send(0x48, 0, true); }
  bool queryFlashTotal()   { return send(0x49, 0, true); }
  bool queryTFCurTrack()   { return send(0x4B, 0, true); }
  bool queryUCurTrack()    { return send(0x4C, 0, true); }
  bool queryFlashCurTrack(){ return send(0x4D, 0, true); }

  // ======== Polling: call in loop() to parse inbound frames ========
  void update() {
    if (!_serial) return;
    while (_serial->available()) {
      uint8_t b = (uint8_t)_serial->read();
      parseByte(b);
    }
  }

  // Busy (if a BUSY pin is wired). Returns true if "playing".
  bool isPlayingBusyPin() const {
    if (_busyPin == 0xFF) return false;
    int v = digitalRead(_busyPin);
    // Datasheet is inconsistent; allow configurable active level.
    // If busy is active‑LOW when playing, then playing = (v == LOW).
    return _busyActiveLow ? (v == LOW) : (v == HIGH);
  }

  // ======== Event queue ========
  bool available() const { return _queueHead != _queueTail; }
  Event readEvent() {
    Event ev{};
    if (!available()) return ev;
    ev = _queue[_queueTail];
    _queueTail = (uint8_t)((_queueTail + 1) & (QUEUE_SZ-1));
    return ev;
  }

  // Send a raw command (advanced)
  bool send(uint8_t cmd, uint16_t param, bool feedback) {
    if (!_serial) return false;
    uint8_t frame[10];
    frame[0] = 0x7E;
    frame[1] = 0xFF;
    frame[2] = 0x06;
    frame[3] = cmd;
    frame[4] = feedback ? 0x01 : 0x00;
    frame[5] = (uint8_t)(param >> 8);
    frame[6] = (uint8_t)(param & 0xFF);

    uint16_t sum = (uint16_t)frame[1] + frame[2] + frame[3] + frame[4] + frame[5] + frame[6];
    uint16_t chk = (uint16_t)(0 - (int16_t)sum); // two's complement
    frame[7] = (uint8_t)(chk >> 8);
    frame[8] = (uint8_t)(chk & 0xFF);
    frame[9] = 0xEF;

    size_t w = _serial->write(frame, sizeof(frame));
    return w == sizeof(frame);
  }

private:
  // Simple state machine to parse 10‑byte frames
  void parseByte(uint8_t b) {
    if (_bufIndex == 0) {
      if (b != 0x7E) return;
      _buf[0] = b;
      _bufIndex = 1;
      return;
    }
    _buf[_bufIndex++] = b;
    if (_bufIndex >= 10) {
      // Verify end
      if (_buf[9] == 0xEF) {
        // Optional checksum verify
        uint16_t sum = (uint16_t)_buf[1] + _buf[2] + _buf[3] + _buf[4] + _buf[5] + _buf[6];
        uint16_t chk = (uint16_t)(0 - (int16_t)sum);
        if (((uint8_t)(chk >> 8) == _buf[7]) && ((uint8_t)(chk & 0xFF) == _buf[8])) {
          pushEventFromFrame(_buf);
        }
        // else checksum mismatch: drop silently (or push error if desired)
      }
      resetParser();
    }
  }

  void resetParser() { _bufIndex = 0; }

  void pushEventFromFrame(const uint8_t* f) {
    uint8_t cmd = f[3];
    uint16_t param = (uint16_t(f[5])<<8) | f[6];
    Event ev{};
    ev.type = (EventType)cmd;
    ev.param = param;
    memcpy(ev.raw, f, 10);
    pushEvent(ev);
  }

  void pushEvent(const Event &e) {
    uint8_t nextHead = (uint8_t)((_queueHead + 1) & (QUEUE_SZ-1));
    if (nextHead == _queueTail) {
      // overflow: drop oldest
      _queueTail = (uint8_t)((_queueTail + 1) & (QUEUE_SZ-1));
    }
    _queue[_queueHead] = e;
    _queueHead = nextHead;
  }

  void clearEvents() { _queueHead = _queueTail = 0; }

  static const uint8_t QUEUE_SZ = 8;
  Stream* _serial;
  uint8_t _busyPin;
  bool    _busyActiveLow;

  // parser buffer
  uint8_t _buf[10];
  uint8_t _bufIndex;

  // ring queue
  Event _queue[QUEUE_SZ];
  uint8_t _queueHead, _queueTail;
};

#endif // DFPMINI_H
