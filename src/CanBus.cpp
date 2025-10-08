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

  // Filters: CCID(0x338), KEY(0x23A), DOORS(0x2FC)
  _can.init_Mask(0, 0, 0x7FF);
  _can.init_Filt(0, 0, ID_CCID);
  _can.init_Filt(1, 0, ID_KEYBTN);

  _can.init_Mask(1, 0, 0x7FF);
  _can.init_Filt(2, 0, ID_DOORS2);
  _can.init_Filt(3, 0, ID_CCID);
  _can.init_Filt(4, 0, ID_KEYBTN);
  _can.init_Filt(5, 0, ID_DOORS2);

  _can.setMode(MCP_NORMAL);
  pinMode(PIN_CAN_INT, INPUT);
  return true;
}

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
