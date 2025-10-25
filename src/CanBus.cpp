#include "CanBus.h"

CanBus::CanBus(uint8_t csPin)
: _can(csPin), _histHead(0), _historyDepth(10), _dedupWindowMs(300) {
  for(uint8_t i=0;i<MAX_HISTORY;i++) _hist[i].valid=false;
}

bool CanBus::begin() {
  pinMode(PIN_CAN_CS, OUTPUT);
  digitalWrite(PIN_CAN_CS, HIGH);
  SPI.begin();

if (_can.begin(MCP_ANY, CAN_100KBPS, MCP_8MHZ) != CAN_OK)
  return false;

// Filters: CCID, KEYBTN, DOORS2, KL15, HANDBRAKE, AIRBAG, BUTTON
_can.init_Mask(0, 0, 0x7FF);           // Full 11-bit mask (standard frames)
_can.init_Filt(0, 0, ID_CCID);
_can.init_Filt(1, 0, ID_KEYBTN);

_can.init_Mask(1, 0, 0x7FF);
_can.init_Filt(2, 0, ID_DOORS2);
_can.init_Filt(3, 0, ID_KL15);
_can.init_Filt(4, 0, ID_HANDBRAKE);
_can.init_Filt(5, 0, ID_BUTTON);      
  

  _can.setMode(MCP_NORMAL);
  pinMode(PIN_CAN_INT, INPUT);

  return true;
}


// ================= raw + de-dup =================
bool CanBus::readRaw(uint32_t &id, uint8_t &len, uint8_t *buf){
  if(_can.checkReceive() != CAN_MSGAVAIL) return false;
  unsigned long _id; uint8_t _len; uint8_t _buf[8];
  if(_can.readMsgBuf(&_id, &_len, _buf) != CAN_OK) return false;
  id=_id; len=_len; for(uint8_t i=0;i<_len && i<8;i++) buf[i]=_buf[i];
  return true;
}
bool CanBus::isDuplicate(uint32_t id, uint8_t len, const uint8_t *buf, uint32_t now) const{
  const uint16_t win=_dedupWindowMs; const uint8_t depth=_historyDepth;
  for(uint8_t i=0;i<depth && i<MAX_HISTORY;i++){
    const FrameRec&r=_hist[i]; if(!r.valid) continue;
    if((uint32_t)(now - r.t) > win) continue;
    if(r.id!=id || r.len!=len) continue;
    bool same=true; for(uint8_t b=0;b<len;b++){ if(r.data[b]!=buf[b]){ same=false; break; } }
    if(same) return true;
  }
  return false;
}
void CanBus::pushHistory(uint32_t id, uint8_t len, const uint8_t *buf, uint32_t now){
  FrameRec&r=_hist[_histHead];
  r.id=id; r.len=len; r.t=now; r.valid=true;
  for(uint8_t i=0;i<len && i<8;i++) r.data[i]=buf[i];
  _histHead++; if(_histHead>=MAX_HISTORY) _histHead=0;
}
bool CanBus::readOnceDistinct(uint32_t &id, uint8_t &len, uint8_t *buf){
  uint8_t pulls=0;
  while(pulls<6){
    if(!readRaw(id,len,buf)) return false;
    pulls++;
    uint32_t now=millis();
    if(!isDuplicate(id,len,buf,now)){
      pushHistory(id,len,buf,now);
      return true;
    }
  }
  return false;
}

// ================= KOMBI sweep (unchanged) =================
void CanBus::updateKL15_fromB0(uint8_t b0){
  if(b0!=_lastB0) _lastB0=b0;
  bool acc=(b0&0x01), run15=(b0&0x04), start=(b0&0x08);
  bool newOn = run15 || start || (_sweepAcceptACC && acc);
  if(newOn && !_kl15On){ _sweepArmed=true; _armTime=millis(); }
  _kl15On=newOn;
}
void CanBus::sendKombiPL(const uint8_t* pl, uint8_t len){
  uint8_t buf[8]={0}; buf[0]=0x60; buf[1]=len;
  for(uint8_t i=0;i<len && i<6;i++) buf[2+i]=pl[i];
  _can.sendMsgBuf(0x6F1, 0, 2+len, buf);
}
void CanBus::sendBurst(const uint8_t* pl, uint8_t len){ for(uint8_t i=0;i<2;i++){ sendKombiPL(pl,len); delay(30); } }
const uint8_t SPD_STOP[] = {0x30,0x20,0x00};
const uint8_t TAC_STOP[] = {0x30,0x21,0x00};
void CanBus::spdStop(){ sendBurst(SPD_STOP,sizeof(SPD_STOP)); }
void CanBus::tacStop(){ sendBurst(TAC_STOP,sizeof(TAC_STOP)); }
uint16_t CanBus::kmh_to_raw(uint16_t kmh){ uint32_t n=(uint32_t)kmh*1777u; return (uint16_t)((n+50u)/100u); }
uint16_t CanBus::rpm_to_raw(uint16_t rpm){ uint32_t n=(uint32_t)rpm*106u;  return (uint16_t)((n+62u)/125u); }
void CanBus::spdMax(){ uint16_t r=kmh_to_raw(_targetKmh); uint8_t pl[5]={0x30,0x20,0x06,(uint8_t)(r>>8),(uint8_t)r}; sendBurst(pl,5); }
void CanBus::tacMax(){ uint16_t r=rpm_to_raw(_targetRpm); uint8_t pl[5]={0x30,0x21,0x06,(uint8_t)(r>>8),(uint8_t)r}; sendBurst(pl,5); }
void CanBus::tickSweep(){
  if(!_sweepEnabled) return;
  const uint32_t now=millis();
  if(_sweepArmed && _kl15On && (uint32_t)(now-_armTime)>=_startAfterKL15){
    _sweepArmed=false; _swState=SW_WAIT_DELAY;
    _swDeadline = now + (_spdDelay <= _rpmDelay ? _spdDelay : _rpmDelay);
  }
  if(_swState==SW_IDLE) return;
  if(!_kl15On){ _swState=SW_IDLE; return; }
  if((int32_t)(now - _swDeadline) < 0) return;

  switch(_swState){
    case SW_WAIT_DELAY:
      if(_spdDelay <= _rpmDelay){ spdMax(); _swDeadline=now+(_rpmDelay-_spdDelay); }
      else                      { tacMax(); _swDeadline=now+(_spdDelay-_rpmDelay); }
      _swState=SW_TO_MAX_2; break;
    case SW_TO_MAX_2:
      if(_spdDelay <= _rpmDelay) tacMax(); else spdMax();
      _swDeadline=now+_peakDwell; _swState=SW_PEAK; break;
    case SW_PEAK:
      _swDeadline=now+(_spdDelay <= _rpmDelay ? _spdDelay : _rpmDelay);
      _swState=SW_TO_STOP_1; break;
    case SW_TO_STOP_1:
      if(_spdDelay <= _rpmDelay){ spdStop(); _swDeadline=now+(_rpmDelay-_spdDelay); }
      else                      { tacStop(); _swDeadline=now+(_spdDelay-_rpmDelay); }
      _swState=SW_TO_STOP_2; break;
    case SW_TO_STOP_2:
      if(_spdDelay <= _rpmDelay) tacStop(); else spdStop();
      _swState=SW_IDLE; break;
    default: _swState=SW_IDLE; break;
  }
}

// ================= CHANGE-DRIVEN SUBSYSTEMS =================

// ---- DOORS (BMW 0x2FC): rx[1] bits 0,2,4,6 ; rx[2] bits 0,2
void CanBus::pushDoorEvent(DoorEventType t, uint32_t now){
  uint8_t next = (uint8_t)((_dqHead + 1) % DOOR_Q_CAP);
  if(next == _dqTail) _dqTail = (uint8_t)((_dqTail + 1) % DOOR_Q_CAP); // drop oldest if full
  _doorQ[_dqHead] = { t, now };
  _dqHead = next;
}
void CanBus::handleDoors_2FC(const uint8_t *buf, uint8_t len, uint32_t now){
  if(len < 3) return;
  DoorSnapshot cur;
  cur.driver        = Bit(buf,1,0x01);
  cur.passenger     = Bit(buf,1,0x04);
  cur.rearDriver    = Bit(buf,1,0x10);
  cur.rearPassenger = Bit(buf,1,0x40);
  cur.boot          = Bit(buf,2,0x01);
  cur.bonnet        = Bit(buf,2,0x04);

  auto emit = [&](bool prev, bool nowb, DoorEventType openEv, DoorEventType closeEv){
    if(nowb != prev) pushDoorEvent(nowb ? openEv : closeEv, now);
  };
  emit(_door.driver,        cur.driver,        DoorEventType::DriverOpened,        DoorEventType::DriverClosed);
  emit(_door.passenger,     cur.passenger,     DoorEventType::PassengerOpened,     DoorEventType::PassengerClosed);
  emit(_door.rearDriver,    cur.rearDriver,    DoorEventType::RearDriverOpened,    DoorEventType::RearDriverClosed);
  emit(_door.rearPassenger, cur.rearPassenger, DoorEventType::RearPassengerOpened, DoorEventType::RearPassengerClosed);
  emit(_door.boot,          cur.boot,          DoorEventType::BootOpened,          DoorEventType::BootClosed);
  emit(_door.bonnet,        cur.bonnet,        DoorEventType::BonnetOpened,        DoorEventType::BonnetClosed);

  _door = cur; // store snapshot
}
bool CanBus::nextDoorEvent(DoorEvent &ev){
  if(_dqTail == _dqHead) return false;
  ev = _doorQ[_dqTail];
  _dqTail = (uint8_t)((_dqTail + 1) % DOOR_Q_CAP);
  return true;
}

// ---- HANDBRAKE (BMW 0x1B4): bit1 of byte5
void CanBus::pushHbEvent(HandbrakeEventType t, uint32_t now){
  uint8_t next=(uint8_t)((_hbHead+1)%HB_Q_CAP);
  if(next==_hbTail) _hbTail=(uint8_t)((_hbTail+1)%HB_Q_CAP);
  _hbQ[_hbHead] = { t, now };
  _hbHead = next;
}
void CanBus::handleHandbrake_1B4(const uint8_t *buf, uint8_t len, uint32_t now){
  if(len <= 5) return;
  bool engaged = ((buf[5] & 0x02) != 0);
  if(engaged != _handbrakeEngaged){
    _handbrakeEngaged = engaged;
    pushHbEvent(engaged ? HandbrakeEventType::Engaged : HandbrakeEventType::Released, now);
  }
}
bool CanBus::nextHandbrakeEvent(HandbrakeEvent &ev){
  if(_hbTail == _hbHead) return false;
  ev = _hbQ[_hbTail];
  _hbTail = (uint8_t)((_hbTail + 1) % HB_Q_CAP);
  return true;
}

// ---- KEY/LOCK (BMW 0x23A): byte2==1 unlock, 4 lock, 64 trunk; with cooldown
void CanBus::pushKeyEvent(KeyEventType t, uint32_t now){
  uint8_t next=(uint8_t)((_keyHead+1)%KEY_Q_CAP);
  if(next==_keyTail) _keyTail=(uint8_t)((_keyTail+1)%KEY_Q_CAP);
  _keyQ[_keyHead] = { t, now };
  _keyHead = next;
}
void CanBus::handleKey_23A(const uint8_t *buf, uint8_t len, uint32_t now){
  if(len < 3) return;

  const uint8_t keyRaw = buf[2];
  // cooldown against repeated frames from same press
  if(keyRaw == _lastKeyRaw && (uint32_t)(now - _lastKeyMs) < _keyCooldownMs) return;

  KeyEventType type = KeyEventType::Other;
  if(keyRaw == 1)      type = KeyEventType::Unlock;
  else if(keyRaw == 4) type = KeyEventType::Lock;
  else if(keyRaw == 64)type = KeyEventType::Trunk;

  // push event if meaningful or raw value changed
  if(type != KeyEventType::Other || keyRaw != _lastKeyRaw){
    pushKeyEvent(type, now);
    _lastKeyRaw = keyRaw;
    _lastKeyMs  = now;

    // maintain lock/unlock snapshot
    if(type == KeyEventType::Unlock) _keySnap.lockState = LockState::Unlocked;
    else if(type == KeyEventType::Lock) _keySnap.lockState = LockState::Locked;

    _keySnap.raw = keyRaw;
    _keySnap.t_ms = now;
  }
}
bool CanBus::nextKeyEvent(KeyEvent &ev){
  if(_keyTail == _keyHead) return false;
  ev = _keyQ[_keyTail];
  _keyTail = (uint8_t)((_keyTail + 1) % KEY_Q_CAP);
  return true;
}

// --- Voltage decode helper (0x3B4) ---
void CanBus::handleVoltage_3B4(const uint8_t* buf, uint8_t len){
  if(len < 3) return;
  uint16_t raw = (uint16_t)(((buf[1] & 0x0F) << 8) | buf[0]);
  float v = 0.0147059f * raw;      // same scale you’re using
  _lastVoltage = v;
  _voltageSeen = true;
}

// --- dispatcher (single, merged) ---
void CanBus::onFrame(uint32_t id, uint8_t len, const uint8_t *buf){
  const uint32_t now = millis();

  if      (id == ID_KL15     && len>=1) updateKL15_fromB0(buf[0]);
  else if (id == ID_DOORS2   && len>=3) handleDoors_2FC(buf, len, now);
  else if (id == ID_HANDBRAKE&& len>=6) handleHandbrake_1B4(buf, len, now);
  else if (id == ID_KEYBTN   && len>=3) handleKey_23A(buf, len, now);
  else if (id == 0x3B4       && len>=3) handleVoltage_3B4(buf, len);  // battery voltage
}


// --- Blocking convenience: wait for a 0x3B4 up to timeoutMs ---
bool CanBus::readVoltage(float& outV, uint16_t timeoutMs){
  uint32_t start = millis();
  do {
    uint32_t id; uint8_t len; uint8_t buf[8];
    if(readOnceDistinct(id, len, buf)){
      // keep other subsystems in sync
      onFrame(id, len, buf);

      if(id == 0x3B4 && len >= 3){
        uint16_t raw = (uint16_t)(((buf[1] & 0x0F) << 8) | buf[0]);
        outV = 0.0147059f * raw;
        _lastVoltage = outV;
        _voltageSeen = true;
        return true;
      }
    }
  } while((uint32_t)(millis()-start) < timeoutMs);

  return false;
}
