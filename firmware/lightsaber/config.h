// Build-time configuration. Everything a user might want to tune lives here;
// runtime settings (volume, brightness, colour...) are in the on-saber menu.
#pragma once

// ---------------------------------------------------------------- pins (V3 PCB, as built)
#define PIN_BTN_MAIN 2   // J2, INT0
#define PIN_BTN_AUX  3   // J4, INT1
#define PIN_LED      6   // J1 WS2812B data
// Fixed by the drivers (direct port access, change there too if you move them):
//   OLED  SDA = D8 (PB0), SCL = D9 (PB1)          -> oled.cpp
//   DFPlayer  RX <- D11 (PB3), TX -> D10 (PB2)     -> audio.cpp
//   MPU-6050 on hardware I2C A4/A5                 -> twi.cpp

// ---------------------------------------------------------------- blade
#define NUM_LEDS        144   // must stay <= 255 (uint8_t indices)
#define LED_MAX_MA      3200  // strip current budget. A solid blue blade is ~2.9 A at full
                              // brightness; lower this if the cell or its protection trips
#define BLADE_FPS_CAP   125   // frames per second ceiling for the strip

// ---------------------------------------------------------------- battery
// The cell feeds the Nano's 5V pin directly, so the Nano's own supply IS the cell: power.cpp
// measures Vcc against the internal 1.1 V bandgap and needs no sense wire. The bandgap is
// only good to +-10 % chip to chip; if the readout is off, measure the cell with a multimeter
// and scale this (new = old * true_mV / shown_mV). Menu > BATT shows the raw millivolts.
#define BANDGAP_MV      1100
#define BATT_LOW_MV     3500  // rested cell below this: warn once (clears above BATT_LOW_MV + 100)
#define BATT_EMPTY_MV   3300  // rested cell below this: lock the blade out
#define BATT_SAG_MV     3000  // blade on and the cell *under load* below this for a while: shut down
#define BATT_USB_MV     4300  // above this we're on USB (reads ~4.4 V), not the cell (max 4.2 V)

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
#define SWING_GAP_MS      700  // minimum time between swing sounds (the clips run up to ~0.9 s)
#define MOTION_DPS        18   // anything above is "the saber is being handled"

// ---------------------------------------------------------------- timing
#define IDLE_OFF_MS       (5UL * 60 * 1000) // blade on but untouched -> auto retract
#define MENU_TIMEOUT_MS   30000
#define SETTINGS_SAVE_MS  2500             // EEPROM write is deferred this long after a change

// ---------------------------------------------------------------- debug
#ifndef DEBUG_SERIAL
#define DEBUG_SERIAL 0  // 1: 115200 baud telemetry
#endif
