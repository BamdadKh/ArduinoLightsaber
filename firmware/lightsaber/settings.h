// Persistent settings (EEPROM) and the colour table.
#pragma once
#include <stdint.h>
#include "platform.h"

#define NUM_PRESETS 6

struct Preset {
  char name[6];
  uint8_t hue, sat;
};

struct Settings {
  uint8_t magic;
  uint8_t volume;     // 0-30 (DFPlayer scale)
  uint8_t bright;     // 1-10
  uint8_t preset;     // colour
  uint8_t sens;       // 1-9, swing and clash together
  uint8_t flip;       // OLED rotated 180
  int16_t gyroBias[3];
  uint8_t check;      // simple checksum of everything above
};

extern Settings cfg;
extern const Preset PRESETS[NUM_PRESETS] PROGMEM;

void settingsLoad();
void settingsDefaults();
void settingsDirty();          // schedule a deferred save
void settingsUpdate(uint32_t now);
void settingsSaveNow();

uint8_t presetHue(uint8_t p);
uint8_t presetSat(uint8_t p);
