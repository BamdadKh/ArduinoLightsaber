// Two-button input: debounce, multi-click counting and hold.
#pragma once
#include <stdint.h>

enum : uint8_t { BTN_MAIN, BTN_AUX };
enum : uint8_t {
  BE_CLICK,     // count = 1..3
  BE_HOLD,      // held past HOLD_MS (still down)
  BE_HOLD_END,  // released after a hold
};

struct ButtonEvent {
  uint8_t btn, type, count;
};

void buttonsInit();
void buttonsUpdate(uint32_t now);
bool buttonsGet(ButtonEvent& e);
// With multi-click off, a click fires on release instead of after the double-click
// window (instant response, but no double clicks).
void buttonMulti(uint8_t b, bool on);
// Drop pending events and ignore any button that is still held.
void buttonsFlush();
