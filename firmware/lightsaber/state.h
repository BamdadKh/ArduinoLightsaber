// Shared runtime state. The saber logic writes it; the blade and UI renderers only read it,
// which is also what lets the simulator drive them without any hardware.
#pragma once
#include <stdint.h>

enum Mode : uint8_t { MODE_BOOT, MODE_OFF, MODE_ON, MODE_MENU };

enum MenuItem : uint8_t {
  MI_VOLUME, MI_BRIGHT, MI_COLOR, MI_SENS, MI_FLIP, MI_CALIB, MI_BATT, MI_EXIT, MI_COUNT
};

struct Sys {
  uint32_t now;
  uint8_t mode;
  bool lockup;
  uint8_t ext;             // blade extension 0-255, mirrored on the OLED
  // effect timestamps, for the OLED flashes
  uint32_t clashAt, blastAt;
  uint8_t blastPos;        // 0-255 along the blade
  // power (see power.cpp): voltages are the cell at rest, so they don't dip when the blade lights
  uint16_t battMv;
  uint8_t battPct;
  bool battLow;            // rested cell is low: one warning, then a blinking icon
  bool battEmpty;          // blade locked out until the cell recovers
  bool onBattery;          // false on USB power, where the readout means nothing
  // peripherals
  uint8_t audio;           // AudioStatus
  bool imuOk;
  // menu
  uint8_t menuItem;
  uint32_t menuEditAt;
  // toast banner
  const char* toast;       // PROGMEM
  int16_t toastNum;        // appended if >= 0
  uint32_t toastUntil;
  uint32_t bootAt;
};

extern Sys sys;
