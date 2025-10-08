#pragma once
#include <Arduino.h>

inline bool isSeatbeltCCID(uint16_t id){
  switch(id){
    case 46: case 91: case 389: case 390: return true;
    default: return false;
  }
}

inline uint16_t trackForCcid(uint16_t id){
  switch(id){
    case 0:   return 23;
    case 46: case 91: case 389: case 390: return 2;
    case 16:  return 3;
    case 17:  return 4;
    case 55:  return 5;
    case 38: case 205: case 66: return 6;
    case 32:  return 7;
    case 25:  return 8;
    case 19:  return 9;
    case 18:  return 10;
    case 14:  return 11;
    case 15:  return 12;
    case 335: return 13;
    case 303: return 15;
    case 113: return 16;
    case 166: return 17;
    case 164: return 18;
    case 275: return 19;
    case 167: return 20;
    case 13:  return 21;
    case 306: case 304: case 229: case 220: return 22;
    case 87: case 88: case 89: case 111: case 114: case 115: case 116: case 117: case 118:
    case 119: case 120: case 121: case 122: case 123: case 124: case 125: case 126: case 127:
    case 128: case 129: case 130: case 131: case 132: case 133: case 134: case 135: case 136:
    case 137: case 138: case 196: case 197: case 345: case 346: case 371: case 372: case 373:
    case 378: case 379: case 380: case 381: return 24;
    case 139: return 25;
    case 143: return 26;
    case 140: return 27;
    case 141: return 28;
    case 142: return 29;
    case 265: return 30;
    default:  return 14;
  }
}
