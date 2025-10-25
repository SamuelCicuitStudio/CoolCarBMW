#include "Device.h"

// =================== CLI ===================
void Device::parseCLI(){
  static String line;
  while(Serial.available()){
    char c = (char)Serial.read();
    if(c=='\r'){ if(Serial.peek()=='\n') (void)Serial.read(); c='\n'; }
    if(c=='\n'){
      line.trim();
      if(line.length()){
        char cmd=line[0];
        if(cmd=='p' || cmd=='P'){
          String digits; for(uint16_t i=1;i<line.length();++i){ char ch=line[i]; if(ch>='0'&&ch<='9') digits+=ch; else if(ch!=' ') { digits=""; break; } }
          long n = digits.length()?digits.toInt():-1;
          if(n>=1 && n<=DF_MAX_MP3){
            if(player.isPlaying()) player.stop();
            player.playTrack((uint16_t)n);
          }
        } else if(cmd=='v' || cmd=='V'){
          if(line.length()==2 && (line[1]=='?')){ Serial.print(F("[CLI] volume=")); Serial.println(player.volume()); }
          else if(line.length()==2 && (line[1]=='+'||line[1]=='-')){
            int cur=(int)player.volume(); if(line[1]=='+') cur++; else cur--; if(cur<0)cur=0; if(cur>Player::DF_VOLUME_MAX)cur=Player::DF_VOLUME_MAX;
            player.setVolume((uint8_t)cur); Serial.print(F("[CLI] volume=")); Serial.println(player.volume());
          } else {
            String digits; for(uint16_t i=1;i<line.length();++i){ char ch=line[i]; if(ch>='0'&&ch<='9') digits+=ch; else if(ch!=' ') { digits=""; break; } }
            long v = digits.length()?digits.toInt():-1;
            if(v>=0 && v<=Player::DF_VOLUME_MAX){ player.setVolume((uint8_t)v); Serial.print(F("[CLI] volume set ")); Serial.println(player.volume()); }
          }
        }
      }
      line="";
    } else { line += c; if(line.length()>40) line.remove(0, line.length()-40); }
  }
}

// =================== Helpers ===================
void Device::radioSet(bool on){
  pinMode(PIN_RADIO_HOLD, OUTPUT);
  digitalWrite(PIN_RADIO_HOLD, on ? HIGH : LOW);
  Serial.print(F("[RADIO] ")); Serial.println(on?F("HIGH"):F("LOW"));
}

void Device::playWelcome(){
  if(player.isPlaying()) player.stop();
  player.playTrack(1);
  nowPlaying = NowPlaying::Welcome;
  DBG(F("[PLAY] Welcome T1"));
}

void Device::playTrackNow(uint16_t tr){
  if(player.isPlaying()) player.stop();
  player.playTrack(tr);
  nowPlaying = NowPlaying::Other;
  Serial.print(F("[PLAY] T")); Serial.println(tr);
}

void Device::ensureSeatbeltLoop(){
  // Filter keeps seatbelt state up to date from CC-ID; loop T2 when active.
  if(!filter.state().seatbeltActive) return;
  if(nowPlaying == NowPlaying::Welcome) return; // let welcome finish
  if(player.isPlaying() && player.currentTrack()==2) return;
  if(player.isPlaying()) player.stop();
  player.playTrack(2);
  nowPlaying = NowPlaying::Other;
  DBG(F("[PLAY] Seatbelt T2 loop"));
}

void Device::stopIfTrack(uint16_t tr){
  if(player.isPlaying() && player.currentTrack()==tr) player.stop();
}

bool Device::batteryOK(){
  float v = filter.state().batteryV;
  if (isnan(v)) {
    if (FAILSAFE_NO_RADIO) {
      Serial.println(F("[RADIO] No voltage yet -> keep OFF"));
      return false;
    }
    return true; // permissive if you relax FAILSAFE_NO_RADIO
  }
  Serial.print(F("[RADIO] Battery=")); Serial.print(v, 2); Serial.println(F(" V"));
  return (v >= RADIO_MIN_VOLT);
}

// =================== begin/loop ===================
void Device::begin(){
  Serial.begin(115200);
  delay(100);
  DBG(F("Starting Device (Filter + Player)"));

  // CAN+KOMBI via Filter (sweep configured & gated inside Filter)
  if(!filter.begin()){
    DBG(F("MCP2515 init FAIL")); while(1) delay(1000);
  }
  DBG(F("MCP2515 OK (8MHz, 100kbps)"));

  // DFPlayer
  player.setBenchMode(false);
  player.begin();

  // RNG (for Filter's goodbye random choice)
  randomSeed(analogRead(A0));

  // Start with radio OFF
  radioSet(false);

  // KL15 start mirror
  kl15Prev = filter.state().kl15On;
}

void Device::loop(){
  parseCLI();

  // Pump CAN + sweep + policies -> Filter emits intents
  filter.tick();

  // Radio policy on KL15 edge (optional keep-on-after-OFF)
  bool kl15Now = filter.state().kl15On;
  if(kl15Prev && !kl15Now){
    radioSet(true);                      // hold radio ON after engine off (optional)
    radioHeldAfterIgnOff = true;
  } else if(!kl15Prev && kl15Now){
    radioHeldAfterIgnOff = false;
  }
  kl15Prev = kl15Now;

  // SECURITY FIRST (A1/A2/A3)
  {
    Filter::PlayIntent pi;
    if(filter.popSecurity(pi)){
      // Any security intent preempts seatbelt loop
      stopIfTrack(2);

      // HandbrakeWarn or CCID or others: just play the mapped track
      playTrackNow(pi.track);
      return; // one play per loop
    }
  }

  // NOTIFICATIONS (A4+B+Welcome/Goodbye/IgnGong)
  {
    Filter::PlayIntent pi;
    if(filter.popNotification(pi)){
      // Welcome takes precedence: always play immediately
      if (pi.kind == Filter::Kind::Welcome){
        playWelcome();
        return;
      }

      // If Welcome is playing, defer other notifications
      if (nowPlaying == NowPlaying::Welcome){
        qPush(pi.track);                 // store track number only
        return;
      }

      // Play mapped notification now
      playTrackNow(pi.track);
      return;
    }
  }

  // Drain deferred notifications when a track ends
  static bool wasPlaying = false;
  bool isNowPlaying = player.isPlaying();
  if (wasPlaying && !isNowPlaying){
    // If Welcome just ended, drain deferred items first
    uint16_t tr;
    if (qPop(tr)){
      playTrackNow(tr);
    } else {
      // Nothing deferred -> enforce seatbelt loop if needed
      ensureSeatbeltLoop();
      if(!player.isPlaying()) nowPlaying = NowPlaying::None;
    }
  }
  wasPlaying = isNowPlaying;

  // Key events (UNLOCK/LOCK) -> radio power with voltage guard
  CanBus::KeyEvent kev;
  while (filter.nextKeyEvent(kev)){
    if(kev.type == CanBus::KeyEventType::Unlock){
      if (batteryOK()){
        radioSet(true);
        DBG(F("[KEY] UNLOCK -> radio ON (battery OK)"));
      } else {
        radioSet(false);
        DBG(F("[KEY] UNLOCK -> radio SKIPPED (low battery)"));
      }
      radioHeldAfterIgnOff = false;
    } else if(kev.type == CanBus::KeyEventType::Lock){
      radioSet(false);
      radioHeldAfterIgnOff = false;
      DBG(F("[KEY] LOCK -> radio OFF"));
    }
  }

  // Keep seatbelt loop alive when nothing else is playing
  if(!player.isPlaying()) ensureSeatbeltLoop();

  player.loop();
  delay(2);
}
