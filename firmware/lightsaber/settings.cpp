#include "settings.h"
#include "config.h"
#include "state.h"
#include <stddef.h>

Settings cfg;

const Preset PRESETS[NUM_PRESETS] PROGMEM = {
  // name        hue  sat  hue2  style            ignition
  {"AZURE",      160, 255,   0, STYLE_STABLE,   IGN_SCROLL},
  {"JADE",        96, 255,  24, STYLE_FLOW,     IGN_SPARK},
  {"RUBY",         0, 255,   8, STYLE_UNSTABLE, IGN_STUTTER},
  {"IRIS",       195, 255, 230, STYLE_PULSE,    IGN_SCROLL},
  {"BLAZE",       10, 255,  20, STYLE_FIRE,     IGN_SPARK},
  {"AURA",         0, 255,   0, STYLE_RAINBOW,  IGN_PHOTON},
  {"NOVA",       135, 230,  40, STYLE_PLASMA,   IGN_PHOTON},
  {"NEON",       224, 200, 128, STYLE_CANDY,    IGN_SCROLL},
};

const char STYLE_NAMES[NUM_STYLES][6] PROGMEM = {
  "SOLID", "FERAL", "PULSE", "FIRE", "PRISM", "HAZE", "CANDY", "FLOW"
};
const char IGNITION_NAMES[NUM_IGNITIONS][6] PROGMEM = {"SWEEP", "SPARK", "SURGE", "BOLT"};
const uint8_t SLEEP_MINUTES[6] PROGMEM = {1, 2, 5, 10, 30, 0};
const uint8_t GESTURE_MASKS[GESTURE_OPTIONS] = {
  0, GEST_SWING_ON, GEST_TWIST_ON | GEST_TWIST_OFF, GEST_STAB_ON,
  GEST_SWING_ON | GEST_TWIST_ON | GEST_STAB_ON | GEST_TWIST_OFF,
};

#define SETTINGS_MAGIC 0xA7

uint8_t presetSat(uint8_t p) { return pgm_read_byte(&PRESETS[p].sat); }
uint8_t presetHue2(uint8_t p) { return cfg.hue[p] + pgm_read_byte(&PRESETS[p].hue2Offset); }

static uint8_t checksum() {
  const uint8_t* p = (const uint8_t*)&cfg;
  uint8_t c = 0x5A;
  for (uint8_t i = 0; i < sizeof(Settings) - 1; i++) c = (c << 1 | c >> 7) ^ p[i];
  return c;
}

void settingsDefaults(bool keepStats) {
  // calibration and lifetime stats live at the end of the struct and survive a reset
  if (!keepStats) memset(&cfg.axis, 0, sizeof(Settings) - offsetof(Settings, axis));
  memset(&cfg, 0, offsetof(Settings, axis));
  cfg.magic = SETTINGS_MAGIC;
  cfg.volume = 21;
  cfg.bright = 7;
  cfg.swingSens = 5;
  cfg.clashSens = 5;
  cfg.gestures = GEST_TWIST_ON | GEST_TWIST_OFF; // "TWIST": rev the hilt to ignite / retract
  cfg.boost = 1;
  cfg.sleepIdx = 2;
  for (uint8_t i = 0; i < NUM_PRESETS; i++) {
    cfg.hue[i] = pgm_read_byte(&PRESETS[i].hue);
    cfg.style[i] = pgm_read_byte(&PRESETS[i].style);
    cfg.ignition[i] = pgm_read_byte(&PRESETS[i].ignition);
  }
}

#ifdef ARDUINO
#include <EEPROM.h>

static uint32_t dirtyAt;
static bool dirty;

void settingsLoad() {
  EEPROM.get(0, cfg);
  if (cfg.magic != SETTINGS_MAGIC || cfg.check != checksum() || cfg.preset >= NUM_PRESETS) {
    settingsDefaults(false);
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
void settingsLoad() { settingsDefaults(false); }
void settingsSaveNow() {}
void settingsDirty() {}
void settingsUpdate(uint32_t) {}
#endif
