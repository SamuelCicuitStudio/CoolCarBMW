#pragma once
#include <Arduino.h>
#include <SPI.h>
#include "mcp_can.h"
#include "Pins.h"

class CanBus {
public:
  explicit CanBus(uint8_t csPin = PIN_CAN_CS);

  bool begin();

  // returns next *distinct* frame (deduplicated using history + time window)
  bool readOnceDistinct(uint32_t &id, uint8_t &len, uint8_t *buf);

  void setDedupWindow(uint16_t ms) { _dedupWindowMs = ms; }
  void setHistoryDepth(uint8_t depth) { _historyDepth = (depth>10)?10:depth; }

private:
  struct FrameRec {
    uint32_t id;
    uint8_t  len;
    uint8_t  data[8];
    uint32_t t;
    bool     valid;
  };

  MCP_CAN _can;
  static const uint8_t MAX_HISTORY = 10;
  FrameRec _hist[MAX_HISTORY];
  uint8_t  _histHead;
  uint8_t  _historyDepth;
  uint16_t _dedupWindowMs;

  bool readRaw(uint32_t &id, uint8_t &len, uint8_t *buf);
  bool isDuplicate(uint32_t id, uint8_t len, const uint8_t *buf, uint32_t now) const;
  void pushHistory(uint32_t id, uint8_t len, const uint8_t *buf, uint32_t now);
};
