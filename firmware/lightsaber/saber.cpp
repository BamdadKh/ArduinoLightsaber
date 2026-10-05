#include "saber.h"
#include "config.h"
#include "state.h"
#include "settings.h"
#include "buttons.h"
#include "imu.h"
#include "audio.h"
#include "blade.h"
#include "ui.h"
#include "power.h"
#include "mathx.h"

Sys sys;

static const char T_LOWBAT[] PROGMEM = "LOW BATT";
static const char T_EMPTY[] PROGMEM = "BATT EMPTY";
static const char T_CAL_OK[] PROGMEM = "CALIBRATED";
static const char T_CAL_FAIL[] PROGMEM = "HOLD STILL";
static const char T_NOIMU[] PROGMEM = "NO IMU";

#define BOOT_MIN_MS 1900

static uint32_t retractAt, lastInput;
static uint8_t probeTries;
static bool previewLit;
static bool lowWarned;

// Controls
//   blade off   MAIN click: ignite     AUX click: next colour (double: previous)     AUX hold: menu
//   blade on    MAIN click: retract    AUX click: blaster block                      AUX hold: lockup
//   menu        AUX click: next page (double: previous)   MAIN click: change   AUX hold: save and exit
//   automatic   swing sounds, idle auto-retract

// ---------------------------------------------------------------- helpers

static bool isOn() { return sys.mode == MODE_ON; }

static void uiSound(uint8_t mainTrack, uint8_t advert) {
  // with the hum running, UI sounds must be adverts or they'd stop it
  if (isOn()) audioAdvert(advert);
  else audioPlay(mainTrack);
}

static void endLockup(uint32_t now, bool resumeHum) {
  if (!sys.lockup) return;
  sys.lockup = false;
  if (resumeHum) {
    audioLoop(SND_HUM);
    audioAdvertRandom(ADV_CLASH, 4); // parting clash as the blades come apart
    bladeClash(now);
  }
}

static void startLockup(uint32_t now) {
  sys.lockup = true;
  audioLoop(SND_LOCKUP);
  bladeClash(now);
  uiFlash(now);
}

// ---------------------------------------------------------------- transitions

static void enterOff() {
  sys.mode = MODE_OFF;
  buttonMulti(BTN_AUX, true);
  uiRedraw();
}

static void ignite(uint32_t now) {
  if (sys.battEmpty) {
    uiToast(T_EMPTY);
    return;
  }
  sys.mode = MODE_ON;
  sys.lockup = false;
  audioMute(false);
  bladeIgnite(now);
  audioPlayThenLoop(SND_IGNITE, SND_IGNITE_MS, SND_HUM);
  motion.lastMotion = now;
  buttonMulti(BTN_AUX, false); // blaster blocks must be instant
  uiRedraw();
}

static void retract(uint32_t now) {
  if (!isOn()) return;
  endLockup(now, false);
  bladeRetract(now);
  audioPlay(SND_RETRACT);
  retractAt = now;
  enterOff();
}

static void selectPreset(int8_t dir) {
  cfg.preset = (cfg.preset + NUM_PRESETS + dir) % NUM_PRESETS;
  settingsDirty();
  uiToast(PRESETS[cfg.preset].name, -1, 900);
  uiSound(SND_UI_TICK, ADV_PRESET);
}

// ---------------------------------------------------------------- menu

static bool previewItem(uint8_t item) { return item == MI_BRIGHT || item == MI_COLOR; }

static void menuPreview(uint32_t now) {
  bool want = previewItem(sys.menuItem);
  if (want && !previewLit) bladeIgnite(now, true);
  else if (!want && previewLit) bladeRetract(now);
  previewLit = want;
}

static void enterMenu(uint32_t now) {
  sys.mode = MODE_MENU;
  sys.menuItem = 0;
  lastInput = now;
  audioPlay(SND_UI_OK);
  uiRedraw();
}

static void exitMenu(uint32_t now) {
  if (previewLit) bladeRetract(now);
  previewLit = false;
  settingsSaveNow();
  audioPlay(SND_UI_BACK);
  enterOff();
}

static void menuChange(uint32_t now) {
  switch (sys.menuItem) {
    case MI_VOLUME:
      cfg.volume = cfg.volume >= 30 ? 0 : cfg.volume + 3 - cfg.volume % 3;
      audioSetVolume(cfg.volume);
      break;
    case MI_BRIGHT:
      cfg.bright = cfg.bright % 10 + 1;
      bladeSetBrightness(cfg.bright);
      break;
    case MI_COLOR: cfg.preset = (cfg.preset + 1) % NUM_PRESETS; break;
    case MI_SENS: cfg.sens = cfg.sens % 9 + 1; break;
    case MI_FLIP:
      cfg.flip ^= 1;
      uiSetFlip(cfg.flip);
      break;
    case MI_CALIB:
      if (imuCalibrateBias(true)) {
        uiToast(T_CAL_OK);
        audioPlay(SND_UI_OK);
      } else {
        uiToast(T_CAL_FAIL, -1, 2000);
        audioPlay(SND_UI_BACK);
      }
      return;
    case MI_BATT: return; // the page itself is the readout
    default:
      exitMenu(now);
      return;
  }
  sys.menuEditAt = now;
  settingsDirty();
  uiSound(SND_UI_TICK, ADV_TICK);
}

static void menuButton(uint32_t now, const ButtonEvent& e) {
  lastInput = now;
  if (e.btn == BTN_AUX) {
    if (e.type == BE_CLICK) {
      sys.menuItem = (sys.menuItem + (e.count == 2 ? MI_COUNT - 1 : 1)) % MI_COUNT;
      uiSound(SND_UI_TICK, ADV_TICK);
      menuPreview(now);
    } else if (e.type == BE_HOLD) {
      exitMenu(now);
    }
  } else if (e.type == BE_CLICK) {
    menuChange(now);
  }
}

// ---------------------------------------------------------------- event handling per mode

static void offButton(uint32_t now, const ButtonEvent& e) {
  lastInput = now;
  if (e.btn == BTN_MAIN) {
    if (e.type == BE_CLICK) ignite(now);
  } else if (e.type == BE_CLICK) {
    selectPreset(e.count == 2 ? -1 : 1);
  } else if (e.type == BE_HOLD) {
    enterMenu(now);
  }
}

static void onButton(uint32_t now, const ButtonEvent& e) {
  lastInput = now;
  if (e.btn == BTN_MAIN) {
    if (e.type == BE_CLICK) retract(now);
    return;
  }
  switch (e.type) {
    case BE_CLICK:
      if (sys.lockup) break;
      bladeBlast(now, 70 + (hash8((uint16_t)now) % 160));
      audioAdvertRandom(ADV_BLASTER, 4);
      break;
    case BE_HOLD:
      if (!sys.lockup) startLockup(now);
      break;
    case BE_HOLD_END:
      endLockup(now, true);
      break;
  }
}

static void onMotion(uint32_t now, uint8_t ev) {
  if ((ev & IMU_SWING) && bladeSettled() && !sys.lockup) {
    audioAdvertRandom(motion.swingPeak > SWING_FAST_DPS ? ADV_SWING_FAST : ADV_SWING_SLOW, 4);
  }
}

// ---------------------------------------------------------------- main update

void saberInit(uint32_t now) {
  sys.mode = MODE_BOOT;
  sys.bootAt = now;
  sys.audio = AUDIO_UNKNOWN;
  lastInput = now;
  buttonMulti(BTN_MAIN, false); // the ignite click fires on release, with no double-click wait
  audioSetVolume(cfg.volume);
}

void saberUpdate(uint32_t now) {
  sys.now = now;
  uint8_t ev = motion.events;
  motion.events = 0;

  // ---- boot: wait for the DFPlayer to come up, then probe it
  if (sys.mode == MODE_BOOT) {
    uint32_t t = now - sys.bootAt;
    if (sys.audio == AUDIO_UNKNOWN && t > 1100u + probeTries * 450u && probeTries < 4) {
      probeTries++;
      sys.audio = audioProbe();
      if (sys.audio == AUDIO_NO_MODULE && probeTries < 4) sys.audio = AUDIO_UNKNOWN;
      if (sys.audio == AUDIO_OK || sys.audio == AUDIO_NO_CARD) audioPlay(SND_BOOT);
    }
    if (t > BOOT_MIN_MS && (sys.audio != AUDIO_UNKNOWN || t > 3200)) {
      if (!sys.imuOk) uiToast(T_NOIMU, -1, 2500);
      enterOff();
      lastInput = now;
    }
    buttonsFlush();
    return;
  }

  // ---- buttons
  ButtonEvent e;
  while (buttonsGet(e)) {
    if (e.type == BE_PRESS || e.type == BE_LONG || e.type == BE_CHORD) continue;
    switch (sys.mode) {
      case MODE_OFF: offButton(now, e); break;
      case MODE_ON: onButton(now, e); break;
      case MODE_MENU: menuButton(now, e); break;
    }
  }

  // ---- motion and timers
  if (isOn()) {
    onMotion(now, ev);
    uint32_t last = motion.lastMotion > lastInput ? motion.lastMotion : lastInput;
    if (now - last > IDLE_OFF_MS) retract(now);
  } else if (sys.mode == MODE_MENU && now - lastInput > MENU_TIMEOUT_MS) {
    exitMenu(now);
  }
  // ---- battery: one warning per low spell, a lockout only when the cell is really empty
  if (sys.battLow && !lowWarned) {
    lowWarned = true;
    uiToast(T_LOWBAT, -1, 2000);
    if (!isOn()) audioPlay(SND_LOWBATT);
  } else if (!sys.battLow) {
    lowWarned = false;
  }
  if (sys.battEmpty && isOn()) {
    retract(now);
    uiToast(T_EMPTY, -1, 2500);
  }
}
