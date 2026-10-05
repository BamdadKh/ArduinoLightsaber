#include "settings.h"
#include "config.h"
#include "state.h"
#include <stddef.h>

Settings cfg;

const Preset PRESETS[NUM_PRESETS] PROGMEM = {
  {"AZURE", 160, 255},
  {"JADE", 96, 255},
  {"RUBY", 0, 255},
  {"AMBER", 18, 255},
  {"IRIS", 195, 255},
  {"ICE", 140, 120},
};

#define SETTINGS_MAGIC 0xB5

uint8_t presetHue(uint8_t p) { return pgm_read_byte(&PRESETS[p].hue); }
uint8_t presetSat(uint8_t p) { return pgm_read_byte(&PRESETS[p].sat); }

static uint8_t checksum() {
  const uint8_t* p = (const uint8_t*)&cfg;
  uint8_t c = 0x5A;
  for (uint8_t i = 0; i < sizeof(Settings) - 1; i++) c = (c << 1 | c >> 7) ^ p[i];
  return c;
}

void settingsDefaults() {
  memset(&cfg, 0, sizeof(cfg));
  cfg.magic = SETTINGS_MAGIC;
  cfg.volume = 9;
  cfg.bright = 7;
  cfg.sens = 5;
}

#ifdef ARDUINO
#include <EEPROM.h>

static uint32_t dirtyAt;
static bool dirty;

void settingsLoad() {
  EEPROM.get(0, cfg);
  if (cfg.magic != SETTINGS_MAGIC || cfg.check != checksum() || cfg.preset >= NUM_PRESETS ||
      cfg.sens < 1 || cfg.sens > 9) {
    settingsDefaults();
    settingsSaveNow();
  }
}

void settingsSaveNow() {
  cfg.check = checksum();
  // update() only rewrites bytes that changed, so the ~100k-cycle EEPROM lasts
  const uint8_t* p = (const uint8_t*)&cfg;
  for (uint8_t i = 0; i < sizeof(Settings); i++) EEPROM.update(i, p[i]);
  dirty = false;
}

void settingsDirty() {
  dirty = true;
  dirtyAt = millis();
}

void settingsUpdate(uint32_t now) {
  if (dirty && now - dirtyAt > SETTINGS_SAVE_MS) settingsSaveNow();
}
#else
void settingsLoad() { settingsDefaults(); }
void settingsSaveNow() {}
void settingsDirty() {}
void settingsUpdate(uint32_t) {}
#endif
