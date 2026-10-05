#include "buttons.h"
#include "config.h"
#include "platform.h"

#define DEBOUNCE_MS 8
#define MULTI_MS 300
#define HOLD_MS 450

struct Btn {
  uint8_t pin;
  bool raw, down, held, ignore, multi;
  uint8_t clicks;
  uint16_t rawSince, pressAt, releaseAt; // 16-bit ms: only short intervals are measured
};

static Btn btns[2]; // zeroed; pins set in buttonsInit()
static ButtonEvent evq[4];
static uint8_t evHead, evLen;

static void emit(uint8_t b, uint8_t type, uint8_t count) {
  if (evLen < 4) {
    evq[(evHead + evLen) & 3] = {b, type, count};
    evLen++;
  }
}

void buttonsInit() {
  btns[BTN_MAIN].pin = PIN_BTN_MAIN;
  btns[BTN_AUX].pin = PIN_BTN_AUX;
  for (uint8_t i = 0; i < 2; i++) {
    pinMode(btns[i].pin, INPUT_PULLUP);
    btns[i].multi = true;
  }
}

void buttonMulti(uint8_t b, bool on) { btns[b].multi = on; }

void buttonsFlush() {
  evLen = 0;
  for (uint8_t i = 0; i < 2; i++) {
    btns[i].clicks = 0;
    if (btns[i].down) btns[i].ignore = true; // swallow its release
  }
}

bool buttonsGet(ButtonEvent& e) {
  if (!evLen) return false;
  e = evq[evHead];
  evHead = (evHead + 1) & 3;
  evLen--;
  return true;
}

void buttonsUpdate(uint32_t now32) {
  uint16_t now = now32;
  for (uint8_t i = 0; i < 2; i++) {
    Btn& b = btns[i];
    bool raw = !digitalRead(b.pin);
    if (raw != b.raw) {
      b.raw = raw;
      b.rawSince = now;
    } else if (raw != b.down && (uint16_t)(now - b.rawSince) >= DEBOUNCE_MS) {
      b.down = raw;
      if (raw) {
        b.pressAt = now;
        b.held = false;
      } else {
        b.releaseAt = now;
        if (b.ignore) b.ignore = false;
        else if (b.held) emit(i, BE_HOLD_END, 0);
        else if (!b.multi) emit(i, BE_CLICK, 1);
        else b.clicks++;
      }
    }
    if (b.ignore) continue;

    if (b.down) {
      if (!b.held && (uint16_t)(now - b.pressAt) >= HOLD_MS) {
        b.held = true;
        b.clicks = 0;
        emit(i, BE_HOLD, 1);
      }
    } else if (b.clicks && ((uint16_t)(now - b.releaseAt) >= MULTI_MS || b.clicks >= 3)) {
      emit(i, BE_CLICK, b.clicks);
      b.clicks = 0;
    }
  }
}
