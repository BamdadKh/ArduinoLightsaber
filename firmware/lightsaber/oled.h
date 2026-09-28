// SSD1306 128x32 on bit-banged I2C (D8 = SDA, D9 = SCL), used in portrait.
//
// The controller runs in *vertical* addressing mode over pages 0-3, so the data
// stream walks down each 32-pixel column before moving to the next one. With the
// panel standing on end, one physical column is exactly one row of the portrait
// screen: 4 bytes. The UI therefore renders row by row into a uint32_t and streams
// it out, with no framebuffer at all (the 512 B one wouldn't fit next to the LEDs).
#pragma once
#include <stdint.h>

void oledInit(bool flip);
void oledFlip(bool flip);
void oledPower(bool on);
void oledInvert(bool on);
void oledContrast(uint8_t c);
void oledWindowAll();          // rewind the write pointer to the top of the screen
void oledRowsBegin();
void oledRow(uint32_t bits);   // bit 31 = leftmost pixel of the portrait row
void oledRowsEnd();
