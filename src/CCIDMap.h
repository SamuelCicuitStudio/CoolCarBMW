#pragma once
#include <Arduino.h>
#define  DF_MAX_MP3  50 //max sounds


inline uint16_t trackForCcid(uint16_t id) {
  switch (id) {
    case 0: return 23;
    case 46: case 91: case 389: case 390: return 2;
    case 16: return 3;
    case 17: return 4;
    case 55: return 5;
    case 38: case 205: case 66: return 6;
    case 32: return 7;
    case 25: return 8;
    case 19: return 9;
    case 18: return 10;
    case 14: return 11;
    case 15: return 12;
    case 303: return 15;
    case 113: return 16;
    case 166: return 17;
    case 164: return 18;
    case 275: case 286: return 19;
    case 167: return 20;
    case 13: return 21;
    case 306: case 304: case 229: case 220: return 22;
    case 87: case 88: case 89: case 111: case 114: case 115: case 116: case 117: case 118:
    case 119: case 120: case 121: case 122: case 123: case 124: case 125: case 126: case 127:
    case 128: case 129: case 130: case 131: case 132: case 133: case 134: case 135: case 136:
    case 137: case 138: case 196: case 197: case 345: case 346: case 371: case 372: case 373:
    case 378: case 379: case 380: case 381: return 24;
    case 139: case 608: return 25;
    case 143: case 609: return 26;
    case 140: case 610: return 27;
    case 141: case 611: return 28;
    case 142: return 29;
    case 265: return 30;
    case 27: return 31;
    case 28: case 29: case 31: case 49: return 32;
    case 30: case 33: case 39: case 212: case 427: return 33;
    case 182: return 34;
    case 213: return 35;
    case 257: case 367: return 36;
    case 63: case 384: return 37;
    case 71: return 38;
    case 74: return 39;
    case 184: return 40;
    case 36: return 41;
    case 35: case 236: case 237: case 382: return 42;
    case 79: case 165: return 43;
    case 281: case 284: return 44;
    case 1001: return 45;
    case 1002: return 46;
    case 1003: return 47;
    case 1004: return 48;
    case 1005: return 49;
    case 1006: return 50;
    default: return 14;
  }
}

inline bool isSeatbeltCCID(uint16_t id){
  return (id==46 || id==91 || id==389 || id==390);
}

inline bool isBatteryLowCCID(uint16_t id){
  return (id==306 || id==304 || id==229 || id==220);
}

inline bool isLowFuelCCID(uint16_t id){
  return (id==275 || id==286);
}

// Returns true if the given CC-ID is a "battery low" warning
 inline bool isLowBatteryCCID(uint16_t ccid) {
  switch (ccid) {
    case 306:  // battery low
    case 304:
    case 229:
    case 220:
      return true;
    default:
      return false;
  }
}

// ccidStatus: 0x02 = Active, 0x01 = Cleared
static inline bool isLowBatteryActive(uint16_t ccid, uint8_t ccidStatus) {
  return (ccidStatus == 0x02) && isLowBatteryCCID(ccid);
}
