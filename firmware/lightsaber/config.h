// Build-time configuration. Everything a user might want to tune lives here;
// runtime settings (volume, brightness, colours...) are in the on-saber menu.
#pragma once

// ---------------------------------------------------------------- pins (V3 PCB, as built)
#define PIN_BTN_MAIN 2   // J2, INT0
#define PIN_BTN_AUX  3   // J4, INT1
#define PIN_LED      6   // J1 WS2812B data
#define PIN_BATTERY  A7  // J8 battery + (direct, no divider: cell max 4.2 V < Vcc)
// Fixed by the drivers (direct port access, change there too if you move them):
//   OLED  SDA = D8 (PB0), SCL = D9 (PB1)          -> oled.cpp
//   DFPlayer  RX <- D11 (PB3), TX -> D10 (PB2)     -> audio.cpp
//   MPU-6050 on hardware I2C A4/A5                 -> twi.cpp

// ---------------------------------------------------------------- blade
#define NUM_LEDS        144   // must stay <= 255 (uint8_t indices)
#define LED_MAX_MA      2200  // total strip budget; the boost converter is the limit
#define BLADE_FPS_CAP   125   // frames per second ceiling for the strip

// ---------------------------------------------------------------- battery
#define BATTERY_SENSE   1     // 0 if J8 isn't wired to the cell
#define BANDGAP_MV      1100  // measure your Nano's 1.1 V reference for exact voltages
#define BATT_WARN_MV    3450  // warning chirp + blinking icon
#define BATT_CUTOFF_MV  3250  // blade forced off below this (sustained)

// ---------------------------------------------------------------- sound font timing
// Lengths of the non-looping main-channel tracks, in ms. make_sounds.py writes
// sound_lengths.h with the exact values for the bundled font; if you use your own
// font, edit that file (or override here).
#include "sound_lengths.h"
#define AUDIO_CMD_GAP_MS 45   // DFPlayer needs breathing room between commands

// ---------------------------------------------------------------- motion (defaults; menu scales them)
#define SWING_START_DPS   230  // gyro speed that counts as a swing (sensitivity 5)
#define SWING_END_DPS     110
#define SWING_FAST_DPS    620  // peak above this picks the fast swing bank
#define CLASH_G_X10       30   // high-pass accel for a clash, x0.1 g (sensitivity 5)
#define STAB_G_X10        18   // forward thrust along the blade, x0.1 g
#define TWIST_DPS         260  // roll rate for twist gestures
#define MOTION_DPS        18   // anything above is "the saber is being handled"

// ---------------------------------------------------------------- timing
#define IDLE_OFF_MS       (5UL * 60 * 1000) // blade on but untouched -> auto retract
#define MENU_TIMEOUT_MS   30000
#define SETTINGS_SAVE_MS  2500             // EEPROM write is deferred this long after a change

// ---------------------------------------------------------------- debug
#ifndef DEBUG_SERIAL
#define DEBUG_SERIAL 0  // 1: 115200 baud telemetry
#endif
