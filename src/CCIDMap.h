#pragma once
#include <Arduino.h>

// NOTE: keep as-is unless you explicitly raise it elsewhere.
// Tracks 51–53 exist in your list; ensure DF_MAX_MP3 is large enough
// in your build if you want to play them via Player::playTrack().
#define  DF_MAX_MP3  60 // max sounds

/*
  TRACK MAP (requested list)
  0001 welcome ( WORKING )
  0002 seatbelt warning (CCID 46,91,389,390)
  0003 rear left door (CCID 16)
  0004 rear right door (CCID 17)
  0005 parking brake not down (CCID 55)
  0006 key not found (CC-ID 38,205,66)
  0007 fill cap open (CC-ID 32)
  0008 preheating wait (CC-ID 25)
  0009 trunk open (CC-ID 19)
  0010 hood open (CC-ID 18)
  0011 passager door (CC-ID 14)
  0012 driver door (CC-ID 15)
  0013 ignition on gong (kl15) (not mapped to any CC-ID)
  0014 warning gong (any other CC-ID not in this list)
  0015 CLUTCH (CC-ID 303)
  0016 Side lights (CC-ID 113)
  0017 coolant low (CC-ID 166)
  0018 low washer liquid (CC-ID 164)
  0019 LOW FUEL (CC-ID 275,286)
  0020 SET TIME (CC-ID 167)
  0021 dont forget key (CC-ID 13)
  0022 battery low (CC-ID 306,304,229,220,415)
  0023 all system ok (CC-ID 0)
  0024 bulb burned out (CC-ID 87,88,89,111,114,115,116,117,118,119,120,121,122,123,124,125,126,127,
                                  128,129,130,131,132,133,134,135,136,137,138,196,197,345,346,371,372,373,378,379,380,381)
  0025 tyre front left (CC-ID 139,608)
  0026 tyre front right (CC-ID 143,609)
  0027 tyre rear right (CC-ID 140,610)
  0028 tyre rear left (CC-ID 141,611)
  0029 tyre pressure not same (CC-ID 142)
  0030 tyre overfilled (CC-ID 265)
  0031 low oil level (CC-ID 27,28)
  0032 motor power reduced (CC-ID 29,31,49,216)  // (note: 33 moved to track 33 as "fault stop")
  0033 motor fault stop directly (CC-ID 30,33,39,212,427,961,568)
  0034 oil level sensor fault (CC-ID 182)
  0035 alternator fault (CC-ID 213)
  0036 engine overheating (CC-ID 257,367)        // (note: 39 kept in track 33 to avoid duplicate)
  0037 air leak on one of tyres (CC-ID 63,384)
  0038 brake pads worn (CC-ID 71,907)
  0039 low brake fluid (CC-ID 74)
  0040 traction mode active (CC-ID 184)
  0041 traction mode deactivated (CC-ID 36)
  0042 traction control fault (CC-ID 35,236,237,382)
  0043 ice roads (CC-ID 79,165)
  0044 maintenance required (CC-ID 281,284)
  0045 dont forget to fill up
  0046 goodbye driver
  0047 goodbye driver 2
  0048 goodbye driver and passager
  0049 goodbye driver and passager 2
  0050 handbrake not up
  0051 key battery low (CC-ID 67)
  0052 sport mode on (tied to CAN button press later)
  0053 sport mode off (tied to CAN button press later)
*/

inline uint16_t trackForCcid(uint16_t id) {
  switch (id) {
    case 0:   return 23; // all system ok

    // Seatbelt
    case 46: case 91: case 389: case 390: return 2;

    // Doors / trunk / hood
    case 16:  return 3;   // rear left
    case 17:  return 4;   // rear right
    case 19:  return 9;   // trunk
    case 18:  return 10;  // hood
    case 14:  return 11;  // passenger door
    case 15:  return 12;  // driver door

    // Parking brake & key
    case 55:  return 5;   // parking brake not down
    case 38: case 205: case 66: return 6; // key not found
    case 13:  return 21;  // don't forget key

    // Caps / preheat / lights
    case 32:  return 7;   // fill cap open
    case 25:  return 8;   // preheating wait
    case 113: return 16;  // side lights

    // Fluids & coolant & fuel
    case 166: return 17;                // coolant low
    case 164: return 18;                // low washer liquid
    case 275: case 286: return 19;      // low fuel
    case 167: return 20;                // set time

    // Battery low (added 415)
    case 306: case 304: case 229: case 220: case 415: return 22;

    // Bulbs out
    case 87: case 88: case 89: case 111: case 114: case 115: case 116: case 117: case 118:
    case 119: case 120: case 121: case 122: case 123: case 124: case 125: case 126: case 127:
    case 128: case 129: case 130: case 131: case 132: case 133: case 134: case 135: case 136:
    case 137: case 138: case 196: case 197: case 345: case 346: case 371: case 372: case 373:
    case 378: case 379: case 380: case 381: return 24;

    // Tyres
    case 139: case 608: return 25;   // front left
    case 143: case 609: return 26;   // front right
    case 140: case 610: return 27;   // rear right
    case 141: case 611: return 28;   // rear left
    case 142: return 29;             // pressure not same
    case 265: return 30;             // overfilled
    case 63: case 384: return 37;    // air leak

    // Oil / power / faults
    case 27: case 28: return 31;                                   // low oil level
    case 29: case 31: case 49: case 216: return 32;                // motor power reduced
    case 30: case 33: case 39: case 212: case 427: case 961: case 568: return 33; // motor fault stop
    case 182: return 34;                                           // oil level sensor fault
    case 213: return 35;                                           // alternator fault
    case 257: case 367: return 36;                                 // engine overheating

    // Brakes, traction, road/maintenance
    case 71:  return 38;                     // brake pads worn
    case 74:  return 39;                     // low brake fluid
    case 184: return 40;                     // traction mode active
    case 36:  return 41;                     // traction mode deactivated
    case 35: case 236: case 237: case 382: return 42; // traction control fault
    case 79: case 165: return 43;            // ice roads
    case 281: case 284: return 44;           // maintenance required

    // Key battery low
    case 67:  return 51;

    // Custom/synthetic IDs (optional; keep if used elsewhere)
    case 1001: return 45; // don't forget to fill up (reminder)
    case 1002: return 46; // goodbye driver
    case 1003: return 47; // goodbye driver 2
    case 1004: return 48; // goodbye driver & passenger
    case 1005: return 49; // goodbye driver & passenger 2
    case 1006: return 50; // handbrake not up

    default: return 14;   // generic warning gong
  }
}

// Helpers stay the same, with 415 included in battery-low set.
inline bool isSeatbeltCCID(uint16_t id){
  return (id==46 || id==91 || id==389 || id==390);
}

inline bool isBatteryLowCCID(uint16_t id){
  return (id==306 || id==304 || id==229 || id==220 || id==415);
}

inline bool isLowFuelCCID(uint16_t id){
  return (id==275 || id==286);
}

// Returns true if the given CC-ID is a "battery low" warning
inline bool isLowBatteryCCID(uint16_t ccid) {
  switch (ccid) {
    case 306: case 304: case 229: case 220: case 415:
      return true;
    default:
      return false;
  }
}

// ccidStatus: 0x02 = Active, 0x01 = Cleared
static inline bool isLowBatteryActive(uint16_t ccid, uint8_t ccidStatus) {
  return (ccidStatus == 0x02) && isLowBatteryCCID(ccid);
}
