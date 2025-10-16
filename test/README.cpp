/*
  CAN monitor: show passenger seat status (bit 4 of 2nd byte)
  MCP2515 @ 8 MHz, 100 kbps
  CS → D10, INT → D8
*/

#include <SPI.h>
#include <mcp_can.h>

#define CAN_CS  10
#define CAN_INT 8

MCP_CAN CAN(CAN_CS);

void setup() {
  Serial.begin(115200);
  while (!Serial);

  if (CAN.begin(MCP_ANY, CAN_100KBPS, MCP_8MHZ) != CAN_OK) {
    Serial.println(F("MCP2515 init failed — check wiring"));
    while (1);
  }

  // Accept only ID 0x2FA
  CAN.init_Mask(0, 0, 0x7FF);
  CAN.init_Filt(0, 0, ID_AirBag);
  CAN.init_Mask(1, 0, 0x7FF);
  CAN.init_Filt(2, 0, ID_AirBag);

  CAN.setMode(MCP_NORMAL);
  pinMode(CAN_INT, INPUT);

  Serial.println(F("Listening for ID 0x2FA — bit4 of 2nd byte"));
}

void loop() {
  if (!digitalRead(CAN_INT)) {
    long unsigned int rxId;
    unsigned char len = 0;
    unsigned char buf[8];

    if (CAN.readMsgBuf(&rxId, &len, buf) == CAN_OK && rxId == 0x2FA && len >= 2) {
      bool passengerSeated = bitRead(buf[1], 3); // bit 3 of 2nd byte
      if (passengerSeated)
        Serial.println(F("Passenger seated"));
      else
        Serial.println(F("Passenger not seated"));
    }
  }
}
