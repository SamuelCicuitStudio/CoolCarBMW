#pragma once
// ===== Pin assignments for Arduino Nano (ATmega328P) =====

// MCP2515 (8 MHz) @ 100 kbps
#define PIN_CAN_CS    10  // SS must be OUTPUT on AVR
#define PIN_CAN_INT   8   // MCP2515 INT

// DFPlayer Mini
#define PIN_DF_RX     4   // Arduino RX  <- DF TX
#define PIN_DF_TX     3   // Arduino TX  -> DF RX
#define PIN_DF_BUSY   5   // DF BUSY: LOW while playing
#define PIN_DF_EN     7   // MOSFET gate controlling DF power (HIGH = ON)
#define PIN_RADIO_HOLD 9  // Keeps radio powered

// Speaker / Amplifier relay (pop control)
#define PIN_SPK_RELAY 6   // HIGH = relay ON (speaker connected)


// ===== CAN IDs =====
#define ID_CCID        0x338  // CC-ID frames
#define ID_KEYBTN      0x23A  // Key fob button frames
#define ID_DOORS2      0x2FC  // Door bits (driver bit0 of byte1)
#define ID_KL15        0x130  // Ignition/ACC flags in byte0
#define ID_BUTTON      0x315  // Button command frame
#define ID_HANDBRAKE   0x1B4  // Handbrake state
#define ID_AIRBAG      0x2FA  // Airbag frame
#define ID_BATT_CHECK  0x3B4  // battery check
