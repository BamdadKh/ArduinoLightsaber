// Two-button input: debounce, multi-click counting, hold / long hold, chords.
#pragma once
#include <stdint.h>

enum : uint8_t { BTN_MAIN, BTN_AUX };
enum : uint8_t {
  BE_CLICK,        // count = 1..3
  BE_HOLD,         // held past HOLD_MS (still down)
  BE_LONG,         // held past LONG_MS (still down)
  BE_HOLD_END,     // released after a hold
  BE_CHORD,        // both pressed together (btn = the one pressed second)
  BE_PRESS,        // raw press edge (used to wake from sleep)
};

struct ButtonEvent {
  uint8_t btn, type, count;
};

void buttonsInit();
void buttonsUpdate(uint32_t now);
bool buttonsGet(ButtonEvent& e);
bool buttonDown(uint8_t b);
uint16_t buttonHeldMs(uint8_t b, uint32_t now);
// With multi-click off, a click fires on release instead of after the double-click
// window (used for instant blaster blocks).
void buttonMulti(uint8_t b, bool on);
void buttonsFlush();
