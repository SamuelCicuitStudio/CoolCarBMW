#include <Arduino.h>
#include "Device.h"

Device device;

void setup(){
  device.begin();
}

void loop(){
  device.loop();
}
