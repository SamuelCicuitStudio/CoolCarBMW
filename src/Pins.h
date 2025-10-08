#pragma once
// ===== Pin assignments for Arduino Nano (ATmega328P) =====

// MCP2515 (8 MHz) @ 100 kbps
static const uint8_t PIN_CAN_CS   = 10; // SS must be OUTPUT on AVR
static const uint8_t PIN_CAN_INT  = 8;  // MCP2515 INT

// DFPlayer Mini
static const uint8_t PIN_DF_RX    = 4;  // Arduino RX  <- DF TX
static const uint8_t PIN_DF_TX    = 3;  // Arduino TX  -> DF RX
static const uint8_t PIN_DF_BUSY  = 5;  // DF BUSY: LOW while playing
static const uint8_t PIN_DF_EN    = 7;  // MOSFET gate controlling DF power (HIGH = ON)

// Speaker / Amplifier relay (pop control)
static const uint8_t PIN_SPK_RELAY = 6; // HIGH = relay ON (speaker connected)

// ===== CAN IDs =====
static const uint32_t ID_CCID      = 0x338; // CC-ID frames
static const uint32_t ID_KEYBTN    = 0x23A; // Key fob button frames
static const uint32_t ID_DOORS2    = 0x2FC; // Door bits (driver bit0 of byte1)
static const uint32_t ID_KL15      = 0x130; // Ignition/ACC flags in byte0
