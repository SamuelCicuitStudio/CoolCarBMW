/*
  Welcome + CC-ID player (Arduino Nano + MCP2515 + DFPlayer)
  - Welcome: on exact unlock, arm 2 min; if driver door opens in window -> play track 1.
  - CC-ID frames (0x338) with tail FE FE FE:
      * status 0x02 ACTIVE: play mapped track ONCE (until CLEARED seen).
      * status 0x01 CLEARED: re-arm same CC-ID to allow playing again later.
  - Seatbelt (46,91,389,390): play track 2 while ACTIVE; stop immediately on CLEARED;
    if 20.5-min file ends while still ACTIVE, it auto-restarts.
  - Unmapped CC-IDs -> track 14.
  - CAN reader uses 10-frame history + 300 ms window to drop duplicates.
  - Serial CLI:
      * Play: "p1".."p30"
      * Volume: "v0..v30", "v+", "v-", "v?" (query)
*/

#include <Arduino.h>
#include "Pins.h"
#include "CanBus.h"
#include "Player.h"
#include "CCIDMap.h"

static const bool DEBUG_PRINTS = true;
static const uint32_t WELCOME_WINDOW_MS = 120000UL; // 2 minutes

static inline void DBG(const __FlashStringHelper* s){ if(DEBUG_PRINTS) Serial.println(s); }

static bool isExactUnlock_23A(const uint8_t* d, uint8_t len){
  return (len>=4 && d[0]==0x00 && d[1]==0x30 && d[2]==0x04 && d[3]==0x61);
}

static bool driverDoorOpen_2FC(const uint8_t* d, uint8_t len){
  return (len>=2) ? (d[1] & 0x01) : false;
}

CanBus canbus;
Player  player;

bool     welcomeArmed=false;
uint32_t welcomeDeadline=0;

uint16_t lastCcid=0xFFFF;
uint8_t  lastStatus=0xFF;

bool seatbeltActive=false;

static inline bool timeBefore(uint32_t deadline){ return (int32_t)(millis() - deadline) < 0; }

// --- Simple Serial CLI: play + volume ---
void parseCLI(){
  static String line;
  while(Serial.available()){
    char c = (char)Serial.read();
    if(c=='') continue;
    if(c=='
'){
      line.trim();
      if(line.length()){
        char cmd = line[0];
        if(cmd=='p' || cmd=='P'){
          // Play command p1..p30
          String digits;
          for(uint16_t i=1;i<line.length();++i){
            char ch=line[i];
            if(ch>='0' && ch<='9') digits += ch;
            else if(ch!=' ') { digits=""; break; }
          }
          long n = digits.length()?digits.toInt():-1;
          if(n>=1 && n<=30){
            if(player.isPlaying()){
              if(player.currentTrack()!=1 || n==1){
                player.stop();
                player.playTrack((uint16_t)n);
              }
            } else {
              player.playTrack((uint16_t)n);
            }
            if(DEBUG_PRINTS){ Serial.print(F("[CLI] play ")); Serial.println(n); }
          } else {
            DBG(F("[CLI] Use p1..p30"));
          }
        } else if(cmd=='v' || cmd=='V'){
          // Volume command
          if(line.length()==2 && (line[1]=='?' )){
            Serial.print(F("[CLI] volume=")); Serial.println(player.volume());
          } else if(line.length()==2 && (line[1]=='+' || line[1]=='-')){
            int cur = (int)player.volume();
            if(line[1]=='+') cur++; else cur--;
            if(cur<0) cur=0; if(cur>Player::DF_VOLUME_MAX) cur=Player::DF_VOLUME_MAX;
            player.setVolume((uint8_t)cur);
            Serial.print(F("[CLI] volume=")); Serial.println(player.volume());
          } else {
            // v<number>
            String digits;
            for(uint16_t i=1;i<line.length();++i){
              char ch=line[i];
              if(ch>='0' && ch<='9') digits += ch;
              else if(ch!=' ') { digits=""; break; }
            }
            long v = digits.length()?digits.toInt():-1;
            if(v>=0 && v<=Player::DF_VOLUME_MAX){
              player.setVolume((uint8_t)v);
              Serial.print(F("[CLI] volume set to ")); Serial.println(player.volume());
            } else {
              DBG(F("[CLI] volume: v0..v30, v+, v-, v?"));
            }
          }
        } else {
          DBG(F("[CLI] Commands: p1..p30, v0..v30, v+, v-, v?"));
        }
      }
      line = "";
    } else {
      line += c;
      if(line.length()>32) line.remove(0, line.length()-32);
    }
  }
}

void setup(){
  Serial.begin(115200);
  delay(80);
  DBG(F("Welcome + CCID DFPlayer starting (dedup + CLI + volume)..."));

  if(!canbus.begin()){
    DBG(F("MCP2515 init FAIL."));
    while(1) delay(1000);
  }
  DBG(F("MCP2515 OK (8MHz, 100kbps)."));

  player.setBenchMode(false);
  player.begin();
  Serial.print(F("Player ready. CLI: p1..p30, v0..v30, v+, v-, v?  (vol="));
  Serial.print(player.volume()); Serial.println(')');
}

void loop(){
  // Manual serial first
  parseCLI();

  // CAN processing: distinct frames only
  uint32_t id; uint8_t len; uint8_t buf[8];
  uint8_t processed = 0;
  while(processed < 4 && canbus.readOnceDistinct(id, len, buf)){
    processed++;

    if(id == ID_KEYBTN){
      if(isExactUnlock_23A(buf, len)){
        welcomeArmed = true;
        welcomeDeadline = millis() + WELCOME_WINDOW_MS;
        DBG(F("[WELCOME] armed for 2 minutes."));
      }
    }
    else if(id == ID_DOORS2){
      const bool open = driverDoorOpen_2FC(buf, len);
      if(open && welcomeArmed && timeBefore(welcomeDeadline)){
        if(player.isPlaying()) player.stop();
        player.playTrack(1);
        welcomeArmed = false;
        DBG(F("[WELCOME] played track 1."));
      }
    }
    else if(id == ID_CCID && len>=8){
      if(buf[5]==0xFE && buf[6]==0xFE && buf[7]==0xFE){
        const uint16_t ccid = (uint16_t(buf[1])<<8) | buf[0];
        const uint8_t  st   = buf[2]; // 0x02 ACTIVE, 0x01 CLEARED

        if(isSeatbeltCCID(ccid)){
          if(st == 0x02){
            seatbeltActive = true;
            if(!(player.isPlaying() && player.currentTrack()==2)){
              if(!(player.isPlaying() && player.currentTrack()==1)){
                if(player.isPlaying()) player.stop();
                player.playTrack(2);
              }
            }
          } else if(st == 0x01){
            seatbeltActive = false;
            if(player.isPlaying() && player.currentTrack()==2){
              player.stop();
            }
          }
        } else {
          if(st == 0x02){
            const uint16_t tr = trackForCcid(ccid);
            const bool armed = !(lastCcid == ccid && lastStatus == 0x02);
            if(armed){
              if(player.isPlaying()){
                if(player.currentTrack() != 1){
                  player.stop();
                  player.playTrack(tr);
                }
              } else {
                player.playTrack(tr);
              }
            }
            lastCcid = ccid; lastStatus = 0x02;
          } else if(st == 0x01){
            if(lastCcid == ccid) lastStatus = 0x01;
          }
        }
      }
    }
  }

  // Seatbelt auto-loop
  if(seatbeltActive){
    if(!player.isPlaying()){
      player.playTrack(2);
    } else if(player.currentTrack()!=2 && player.currentTrack()!=1){
      player.stop();
      player.playTrack(2);
    }
  }

  // Welcome expiry
  if(welcomeArmed && !timeBefore(welcomeDeadline)){
    welcomeArmed = false;
    DBG(F("[WELCOME] window expired."));
  }

  // DF housekeeping
  player.loop();
}
