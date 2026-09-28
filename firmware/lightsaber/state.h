// Shared runtime state. The saber logic writes it; the blade and UI renderers only read it,
// which is also what lets the simulator drive them without any hardware.
#pragma once
#include <stdint.h>

enum Mode : uint8_t { MODE_BOOT, MODE_OFF, MODE_ON, MODE_MENU, MODE_SLEEP };
enum Lockup : uint8_t { LOCK_NONE, LOCK_CLASH, LOCK_DRAG, LOCK_MELT, LOCK_LIGHTNING };
enum TrainPhase : uint8_t { TRAIN_WAIT, TRAIN_INCOMING, TRAIN_RESULT, TRAIN_OVER };

enum MenuItem : uint8_t {
  MI_VOLUME, MI_BRIGHT, MI_STYLE, MI_COLOR, MI_IGNITE, MI_SWING, MI_CLASH, MI_GESTURE,
  MI_BOOST, MI_SLEEP, MI_FLIP, MI_TRAIN, MI_CALIB, MI_STATS, MI_RESET, MI_EXIT, MI_COUNT
};

// Gesture presets offered by the menu (the setting itself is a GEST_* bitmask)
#define GESTURE_OPTIONS 5
extern const uint8_t GESTURE_MASKS[GESTURE_OPTIONS];

#define HIST_LEN 48

struct Sys {
  uint32_t now;
  uint8_t mode;
  uint8_t lockup;
  uint32_t lockupAt;
  bool colorWheel, hud, training, muted;
  uint8_t hue;             // live hue of the active preset (colour wheel edits this)
  uint8_t ext;             // blade extension 0-255, mirrored on the OLED
  // effect timestamps, for the OLED flashes
  uint32_t clashAt, blastAt, stabAt, forceAt;
  uint8_t blastPos;        // 0-255 along the blade
  // power
  uint16_t battMv;
  uint8_t battPct;
  bool battLow, battPresent;
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
  // combat telemetry for this session
  uint16_t sessPeak;
  uint8_t combo;
  uint32_t comboAt;
  uint8_t hist[HIST_LEN];  // swing speed history, newest at histHead
  uint8_t histHead;
  // training droid game
  uint8_t trainPhase, trainScore, trainLives, trainPos;
  uint16_t trainReact;     // last reaction time, ms
  uint32_t trainAt;        // phase start
  uint16_t trainWindow;    // current deflect window, ms
  bool trainHit;           // last result
  uint32_t bootAt;
};

extern Sys sys;
