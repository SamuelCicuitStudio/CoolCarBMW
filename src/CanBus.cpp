#include "CanBus.h"

CanBus::CanBus(uint8_t csPin) : _can(csPin),
  _histHead(0),
  _historyDepth(10),
  _dedupWindowMs(300)
{
  for(uint8_t i=0;i<MAX_HISTORY;i++){ _hist[i].valid=false; }
}

bool CanBus::begin(){
  pinMode(PIN_CAN_CS, OUTPUT);
  digitalWrite(PIN_CAN_CS, HIGH);
  SPI.begin();
  if(_can.begin(MCP_ANY, CAN_100KBPS, MCP_8MHZ) != CAN_OK){
    return false;
  }

  // Filters: CCID(0x338), KEY(0x23A), DOORS(0x2FC), KL15(0x130)
  _can.init_Mask(0, 0, 0x7FF);
  _can.init_Filt(0, 0, ID_CCID);
  _can.init_Filt(1, 0, ID_KEYBTN);

  _can.init_Mask(1, 0, 0x7FF);
  _can.init_Filt(2, 0, ID_DOORS2);
  _can.init_Filt(3, 0, ID_KL15);    // include ignition flags
  _can.init_Filt(4, 0, ID_CCID);
  _can.init_Filt(5, 0, ID_KEYBTN);

  _can.setMode(MCP_NORMAL);
  pinMode(PIN_CAN_INT, INPUT);
  return true;
}

// ===== duplicate filter =====
bool CanBus::readRaw(uint32_t &id, uint8_t &len, uint8_t *buf){
  if(_can.checkReceive() != CAN_MSGAVAIL) return false;
  unsigned long _id; uint8_t _len; uint8_t _buf[8];
  if(_can.readMsgBuf(&_id, &_len, _buf) != CAN_OK) return false;
  id = _id; len = _len;
  for(uint8_t i=0;i<_len && i<8;i++) buf[i]=_buf[i];
  return true;
}

bool CanBus::isDuplicate(uint32_t id, uint8_t len, const uint8_t *buf, uint32_t now) const{
  const uint16_t win = _dedupWindowMs;
  const uint8_t depth = _historyDepth;
  for(uint8_t i=0;i<depth && i<MAX_HISTORY;i++){
    const FrameRec &r = _hist[i];
    if(!r.valid) continue;
    if((uint32_t)(now - r.t) > win) continue; // older than window
    if(r.id != id) continue;
    if(r.len != len) continue;
    bool same = true;
    for(uint8_t b=0;b<len;b++){ if(r.data[b] != buf[b]) { same = false; break; } }
    if(same) return true;
  }
  return false;
}

void CanBus::pushHistory(uint32_t id, uint8_t len, const uint8_t *buf, uint32_t now){
  FrameRec &r = _hist[_histHead];
  r.id = id; r.len = len; r.t = now; r.valid = true;
  for(uint8_t i=0;i<len && i<8;i++) r.data[i] = buf[i];
  _histHead++; if(_histHead >= MAX_HISTORY) _histHead = 0;
}

bool CanBus::readOnceDistinct(uint32_t &id, uint8_t &len, uint8_t *buf){
  uint8_t tries = 0;
  while(tries < 6){
    if(!readRaw(id, len, buf)) return false;
    tries++;
    uint32_t now = millis();
    if(!isDuplicate(id, len, buf, now)){
      pushHistory(id, len, buf, now);
      return true;
    }
  }
  return false;
}

// ===== KL15 + sweep =====
void CanBus::updateKL15_fromB0(uint8_t b0){
  if(b0 != _lastB0){ _lastB0 = b0; }
  bool acc   = (b0 & 0x01);
  bool run15 = (b0 & 0x04);
  bool start = (b0 & 0x08);
  bool newOn = run15 || start || (_sweepAcceptACC && acc);
  if(newOn && !_kl15On){ _sweepArmed = true; _armTime = millis(); }
  _kl15On = newOn;
}

void CanBus::onFrame(uint32_t id, uint8_t len, const uint8_t *buf){
  if(id == ID_KL15 && len >= 1){
    updateKL15_fromB0(buf[0]);
  }
}

void CanBus::sendKombiPL(const uint8_t* pl, uint8_t len){
  uint8_t buf[8] = {0}; buf[0]=0x60; buf[1]=len;
  for(uint8_t i=0;i<len && i<6;i++) buf[2+i]=pl[i];
  _can.sendMsgBuf(0x6F1, 0, 2+len, buf);
}

void CanBus::sendBurst(const uint8_t* pl, uint8_t len){
  for(uint8_t i=0;i<2;i++){ sendKombiPL(pl,len); delay(30); }
}

const uint8_t SPD_STOP[] = {0x30,0x20,0x00};
const uint8_t TAC_STOP[] = {0x30,0x21,0x00};

void CanBus::spdStop(){ sendBurst(SPD_STOP,sizeof(SPD_STOP)); }
void CanBus::tacStop(){ sendBurst(TAC_STOP,sizeof(TAC_STOP)); }

uint16_t CanBus::kmh_to_raw(uint16_t kmh){ uint32_t n=(uint32_t)kmh*1777u; return (uint16_t)((n+50u)/100u); }
uint16_t CanBus::rpm_to_raw(uint16_t rpm){ uint32_t n=(uint32_t)rpm*106u;  return (uint16_t)((n+62u)/125u); }

void CanBus::spdMax(){
  uint16_t r=kmh_to_raw(_targetKmh);
  uint8_t pl[5]={0x30,0x20,0x06,(uint8_t)(r>>8),(uint8_t)r};
  sendBurst(pl,5);
}
void CanBus::tacMax(){
  uint16_t r=rpm_to_raw(_targetRpm);
  uint8_t pl[5]={0x30,0x21,0x06,(uint8_t)(r>>8),(uint8_t)r};
  sendBurst(pl,5);
}

void CanBus::tickSweep(){
  if(!_sweepEnabled) return;

  const uint32_t now = millis();

  if(_sweepArmed && _kl15On && (uint32_t)(now - _armTime) >= _startAfterKL15){
    _sweepArmed = false;
    _swState = SW_WAIT_DELAY;
    _swDeadline = now + (_spdDelay <= _rpmDelay ? _spdDelay : _rpmDelay);
  }

  if(_swState == SW_IDLE) return;
  if(!_kl15On){ _swState = SW_IDLE; return; }

  if((int32_t)(now - _swDeadline) < 0) return;

  switch(_swState){
    case SW_WAIT_DELAY:
      if(_spdDelay <= _rpmDelay){ spdMax(); _swDeadline = now + (_rpmDelay - _spdDelay); _swState = SW_TO_MAX_2; }
      else                      { tacMax(); _swDeadline = now + (_spdDelay - _rpmDelay); _swState = SW_TO_MAX_2; }
      break;
    case SW_TO_MAX_2:
      if(_spdDelay <= _rpmDelay) tacMax(); else spdMax();
      _swDeadline = now + _peakDwell;
      _swState = SW_PEAK;
      break;
    case SW_PEAK:
      _swDeadline = now + (_spdDelay <= _rpmDelay ? _spdDelay : _rpmDelay);
      _swState = SW_TO_STOP_1;
      break;
    case SW_TO_STOP_1:
      if(_spdDelay <= _rpmDelay){ spdStop(); _swDeadline = now + (_rpmDelay - _spdDelay); }
      else                      { tacStop(); _swDeadline = now + (_spdDelay - _rpmDelay); }
      _swState = SW_TO_STOP_2;
      break;
    case SW_TO_STOP_2:
      if(_spdDelay <= _rpmDelay) tacStop(); else spdStop();
      _swState = SW_IDLE;
      break;
    default: _swState = SW_IDLE; break;
  }
}
