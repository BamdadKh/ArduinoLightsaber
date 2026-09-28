// Persistent settings and lifetime stats (EEPROM), plus the preset table.
#pragma once
#include <stdint.h>
#include "platform.h"

#define NUM_PRESETS 8
#define NUM_STYLES 8
#define NUM_IGNITIONS 4

enum BladeStyle : uint8_t {
  STYLE_STABLE, STYLE_UNSTABLE, STYLE_PULSE, STYLE_FIRE,
  STYLE_RAINBOW, STYLE_PLASMA, STYLE_CANDY, STYLE_FLOW
};
enum Ignition : uint8_t { IGN_SCROLL, IGN_SPARK, IGN_STUTTER, IGN_PHOTON };

// Gesture bits
enum : uint8_t {
  GEST_SWING_ON = 0x01,  // swing while off -> ignite
  GEST_TWIST_ON = 0x02,  // twist while off -> ignite
  GEST_STAB_ON  = 0x04,  // thrust while off -> ignite
  GEST_TWIST_OFF = 0x08, // twist while on (and still) -> retract
};

struct Preset {
  char name[6];
  uint8_t hue, sat, hue2Offset, style, ignition;
};

struct Settings {
  uint8_t magic;
  uint8_t volume;     // 0-30 (DFPlayer scale)
  uint8_t bright;     // 1-10
  uint8_t preset;
  uint8_t swingSens;  // 1-9
  uint8_t clashSens;  // 1-9
  uint8_t gestures;   // GEST_* bits
  uint8_t boost;      // 0-3 hum swell on swings
  uint8_t sleepIdx;   // index into SLEEP_MINUTES
  uint8_t flip;       // OLED rotated 180
  uint8_t hue[NUM_PRESETS];
  uint8_t style[NUM_PRESETS];
  uint8_t ignition[NUM_PRESETS];
  // --- kept across a factory reset from here on
  uint8_t axis;       // blade axis: (0 x, 1 y, 2 z) * 2 + negative
  int16_t gyroBias[3];
  // lifetime stats
  uint16_t ignitions;
  uint16_t clashes;
  uint32_t onSeconds;
  uint16_t peakDps;
  uint8_t trainBest;
  uint8_t check;      // simple checksum of everything above
};

extern Settings cfg;
extern const Preset PRESETS[NUM_PRESETS] PROGMEM;
extern const char STYLE_NAMES[NUM_STYLES][6] PROGMEM;
extern const char IGNITION_NAMES[NUM_IGNITIONS][6] PROGMEM;
extern const uint8_t SLEEP_MINUTES[6] PROGMEM; // 0 = never

void settingsLoad();
void settingsDefaults(bool keepStats);
void settingsDirty();          // schedule a deferred save
void settingsUpdate(uint32_t now);
void settingsSaveNow();

uint8_t presetSat(uint8_t p);
uint8_t presetHue2(uint8_t p);
