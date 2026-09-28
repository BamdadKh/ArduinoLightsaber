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

static const char T_SAVED[] PROGMEM = "SAVED";
static const char T_BATTERY[] PROGMEM = "BATTERY";
static const char T_CANCEL[] PROGMEM = "CANCEL";
static const char T_MUTED[] PROGMEM = "MUTED";
static const char T_LOWBAT[] PROGMEM = "LOW BATT";
static const char T_EMPTY[] PROGMEM = "BATT EMPTY";
static const char T_CAL_OK[] PROGMEM = "CALIBRATED";
static const char T_CAL_FAIL[] PROGMEM = "BLADE UP!";
static const char T_RESET[] PROGMEM = "DEFAULTS";
static const char T_HUD[] PROGMEM = "TELEMETRY";
static const char T_BLADE[] PROGMEM = "BLADE VIEW";
static const char T_BEST[] PROGMEM = "NEW BEST";
static const char T_TWIST[] PROGMEM = "TWIST HILT";
static const char T_NOIMU[] PROGMEM = "NO IMU";

#define BOOT_MIN_MS 1900

static uint32_t onSince, retractAt, stopAudioAt, lastInput, lastHist, lastWheelTick;
static uint8_t hueBackup;
static uint16_t hueAcc;       // colour wheel accumulator, hue << 7 (wraps with the hue)
static uint8_t probeTries;
static bool previewLit;
static bool cutoffHandled;
static bool lowWarned;

// ---------------------------------------------------------------- helpers

static bool isOn() { return sys.mode == MODE_ON; }

static void uiSound(uint8_t mainTrack, uint8_t advert) {
  // with the hum running, UI sounds must be adverts or they'd stop it
  if (isOn()) audioAdvert(advert);
  else audioPlay(mainTrack);
}

static void loadPresetHue() { sys.hue = cfg.hue[cfg.preset]; }

static void combo(uint32_t now) {
  sys.combo = (now - sys.comboAt < 1200 && sys.combo < 99) ? sys.combo + 1 : 1;
  sys.comboAt = now;
}

static void endLockup(uint32_t now, bool resumeHum) {
  if (!sys.lockup) return;
  sys.lockup = LOCK_NONE;
  if (resumeHum) {
    audioLoop(SND_HUM);
    audioAdvertRandom(ADV_CLASH, 4); // parting clash as the blades come apart
    bladeClash(now);
  }
}

static void startLockup(uint32_t now, uint8_t kind) {
  sys.lockup = kind;
  sys.lockupAt = now;
  static const uint8_t LOOPS[5] PROGMEM = {0, SND_LOCKUP, SND_DRAG, SND_MELT, SND_LIGHTNING};
  audioLoop(pgm_read_byte(&LOOPS[kind]));
  if (kind == LOCK_CLASH || kind == LOCK_DRAG) {
    bladeClash(now);
    uiFlash(now);
  }
}

// ---------------------------------------------------------------- transitions

static void batteryCheck(uint32_t now) {
  uint8_t pct = sys.battPresent ? sys.battPct : 100;
  bladeMeter(now, pct);
  uiToast(T_BATTERY, pct, 2500);
}

static void enterOff() {
  sys.mode = MODE_OFF;
  buttonMulti(BTN_AUX, true);
  uiRedraw();
}

static void ignite(uint32_t now, bool muted) {
  if (powerCritical()) {
    uiToast(T_EMPTY);
    return;
  }
  sys.mode = MODE_ON;
  sys.muted = muted;
  sys.lockup = LOCK_NONE;
  sys.colorWheel = sys.training = false;
  audioMute(muted);
  loadPresetHue();
  bladeIgnite(now);
  audioPlayThenLoop(SND_IGNITE, SND_IGNITE_MS, SND_HUM);
  onSince = now;
  stopAudioAt = 0;
  motion.lastMotion = now;
  cfg.ignitions++;
  buttonMulti(BTN_AUX, false); // blaster blocks must be instant
  if (muted) uiToast(T_MUTED);
  uiRedraw();
}

static void retract(uint32_t now) {
  if (!isOn()) return;
  endLockup(now, false);
  if (sys.colorWheel) sys.hue = hueBackup;
  sys.colorWheel = sys.training = false;
  bladeRetract(now);
  audioSwell(0);
  audioPlay(SND_RETRACT);
  stopAudioAt = now + SND_RETRACT_MS + 400;
  retractAt = now;
  cfg.onSeconds += (now - onSince) / 1000;
  if (sys.sessPeak > cfg.peakDps) cfg.peakDps = sys.sessPeak;
  settingsDirty();
  enterOff();
}

static void goSleep(uint32_t now) {
  if (isOn()) retract(now);
  if (previewLit) bladeRetract(now);
  previewLit = false;
  sys.mode = MODE_SLEEP;
  audioPlay(SND_SLEEP);
  stopAudioAt = now + 1500;
  settingsSaveNow();
  uiSleep(true);
}

static void wake(uint32_t now) {
  enterOff();
  uiSleep(false);
  motion.lastMotion = now;
  lastInput = now;
  buttonsFlush();
}

static void selectPreset(int8_t dir) {
  cfg.preset = (cfg.preset + NUM_PRESETS + dir) % NUM_PRESETS;
  loadPresetHue();
  settingsDirty();
  uiToast(PRESETS[cfg.preset].name, -1, 900);
  uiSound(SND_UI_TICK, ADV_PRESET);
}

// ---------------------------------------------------------------- training droid

static void trainStart(uint32_t now) {
  if (!isOn()) ignite(now, false);
  sys.training = true;
  sys.hud = false;
  sys.trainLives = 3;
  sys.trainScore = 0;
  sys.trainReact = 0;
  sys.trainPhase = TRAIN_WAIT;
  sys.trainAt = now + 2200; // first shot after the ignition settles
}

static void trainDeflect(uint32_t now) {
  if (!sys.training || sys.trainPhase != TRAIN_INCOMING) return;
  sys.trainReact = now - sys.trainAt;
  sys.trainScore++;
  sys.trainHit = false;
  sys.trainPhase = TRAIN_RESULT;
  sys.trainAt = now;
  bladeBlast(now, sys.trainPos);
  audioAdvertRandom(ADV_BLASTER, 4);
}

static void trainUpdate(uint32_t now) {
  if (!sys.training) return;
  int32_t since = now - sys.trainAt;
  switch (sys.trainPhase) {
    case TRAIN_WAIT:
      if (since >= 0) {
        sys.trainPhase = TRAIN_INCOMING;
        sys.trainAt = now;
        sys.trainPos = 70 + (hash8((uint16_t)now) % 160);
        uint16_t w = 950 - sys.trainScore * 30;
        sys.trainWindow = sys.trainScore > 20 ? 350 : w;
        audioAdvert(ADV_TRAIN_SHOT);
      }
      break;
    case TRAIN_INCOMING:
      if (since > sys.trainWindow) {
        sys.trainLives--;
        sys.trainHit = true;
        sys.trainPhase = TRAIN_RESULT;
        sys.trainAt = now;
        bladeHitFlash(now);
        uiFlash(now);
        audioAdvert(ADV_TRAIN_HIT);
      }
      break;
    case TRAIN_RESULT:
      if (since > 700) {
        if (!sys.trainLives) {
          sys.trainPhase = TRAIN_OVER;
          sys.trainAt = now;
          audioAdvert(ADV_TRAIN_OVER);
          if (sys.trainScore > cfg.trainBest) {
            cfg.trainBest = sys.trainScore;
            settingsDirty();
            uiToast(T_BEST, sys.trainScore, 2000);
          }
        } else {
          sys.trainPhase = TRAIN_WAIT;
          sys.trainAt = now + 700 + (hash8((uint16_t)(now >> 3)) << 3);
        }
      }
      break;
    case TRAIN_OVER:
      if (since > 4500) sys.training = false;
      break;
  }
}

// ---------------------------------------------------------------- menu

static bool previewItem(uint8_t item) {
  return item == MI_BRIGHT || item == MI_STYLE || item == MI_COLOR || item == MI_IGNITE;
}

static void menuPreview(uint32_t now) {
  bool want = previewItem(sys.menuItem);
  if (want && !previewLit) {
    loadPresetHue();
    bladeIgnite(now, true);
  } else if (!want && previewLit) {
    bladeRetract(now);
  }
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
  uint8_t p = cfg.preset;
  switch (sys.menuItem) {
    case MI_VOLUME:
      cfg.volume = cfg.volume >= 30 ? 0 : cfg.volume + 3 - cfg.volume % 3;
      audioSetVolume(cfg.volume);
      break;
    case MI_BRIGHT:
      cfg.bright = cfg.bright % 10 + 1;
      bladeSetBrightness(cfg.bright);
      break;
    case MI_STYLE: cfg.style[p] = (cfg.style[p] + 1) % NUM_STYLES; break;
    case MI_COLOR:
      cfg.hue[p] += 16;
      sys.hue = cfg.hue[p];
      break;
    case MI_IGNITE:
      cfg.ignition[p] = (cfg.ignition[p] + 1) % NUM_IGNITIONS;
      bladeIgnite(now); // replay it
      break;
    case MI_SWING: cfg.swingSens = cfg.swingSens % 9 + 1; break;
    case MI_CLASH: cfg.clashSens = cfg.clashSens % 9 + 1; break;
    case MI_GESTURE: {
      uint8_t i = 0;
      while (i < GESTURE_OPTIONS && GESTURE_MASKS[i] != cfg.gestures) i++;
      cfg.gestures = GESTURE_MASKS[(i + 1) % GESTURE_OPTIONS];
      break;
    }
    case MI_BOOST: cfg.boost = (cfg.boost + 1) & 3; break;
    case MI_SLEEP: cfg.sleepIdx = (cfg.sleepIdx + 1) % 6; break;
    case MI_FLIP:
      cfg.flip ^= 1;
      uiSetFlip(cfg.flip);
      break;
    case MI_TRAIN:
      if (previewLit) bladeRetract(now);
      previewLit = false;
      settingsSaveNow();
      sys.mode = MODE_OFF;
      trainStart(now);
      return;
    case MI_CALIB:
      if (imuCalibrateAxis()) {
        uiToast(T_CAL_OK);
        audioPlay(SND_UI_OK);
      } else {
        uiToast(T_CAL_FAIL, -1, 2000);
        audioPlay(SND_UI_BACK);
      }
      return;
    case MI_STATS: return; // the page itself is the readout
    case MI_RESET: return; // needs a long press
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
  } else {
    if (e.type == BE_CLICK) {
      menuChange(now);
    } else if (e.type == BE_LONG && sys.menuItem == MI_RESET) {
      settingsDefaults(true);
      settingsSaveNow();
      audioSetVolume(cfg.volume);
      bladeSetBrightness(cfg.bright);
      uiSetFlip(cfg.flip);
      loadPresetHue();
      uiToast(T_RESET, -1, 1800);
      audioPlay(SND_UI_OK);
    }
  }
}

static void menuUpdate(uint32_t now) {
  if (now - lastInput > MENU_TIMEOUT_MS) exitMenu(now);
}

// ---------------------------------------------------------------- event handling per mode

static void offButton(uint32_t now, const ButtonEvent& e) {
  lastInput = now;
  if (e.btn == BTN_MAIN) {
    if (e.type == BE_CLICK) {
      if (e.count == 1) ignite(now, false);
      else if (e.count == 2) ignite(now, true);
      else batteryCheck(now);
    } else if (e.type == BE_LONG) {
      goSleep(now);
    }
  } else if (e.btn == BTN_AUX) {
    if (e.type == BE_CLICK) selectPreset(e.count == 2 ? -1 : 1);
    else if (e.type == BE_HOLD) enterMenu(now);
  }
  if (e.type == BE_CHORD) batteryCheck(now);
}

static void onButton(uint32_t now, const ButtonEvent& e) {
  lastInput = now;
  if (sys.colorWheel) {
    if (e.type != BE_CLICK) return;
    if (e.btn == BTN_MAIN) {
      cfg.hue[cfg.preset] = sys.hue;
      settingsDirty();
      uiToast(T_SAVED);
      audioAdvert(ADV_OK);
    } else {
      sys.hue = hueBackup;
      uiToast(T_CANCEL);
      audioAdvert(ADV_TICK);
    }
    sys.colorWheel = false;
    return;
  }
  if (e.type == BE_CHORD) {
    bladeForce(now);
    audioAdvert(ADV_FORCE);
    return;
  }
  if (e.btn == BTN_MAIN) {
    switch (e.type) {
      case BE_CLICK:
        if (e.count == 1) {
          if (sys.training) {
            sys.training = false;
            uiRedraw();
          } else {
            retract(now);
          }
        } else if (e.count == 2) {
          sys.colorWheel = true;
          hueBackup = sys.hue;
          hueAcc = (uint16_t)sys.hue << 7;
          uiToast(T_TWIST, -1, 1100);
          audioAdvert(ADV_OK);
        } else {
          sys.hud = !sys.hud;
          uiToast(sys.hud ? T_HUD : T_BLADE, -1, 800);
        }
        break;
      case BE_HOLD:
        if (!sys.lockup) startLockup(now, LOCK_LIGHTNING);
        break;
      case BE_HOLD_END:
        if (sys.lockup == LOCK_LIGHTNING) endLockup(now, true);
        break;
    }
  } else {
    switch (e.type) {
      case BE_CLICK:
        if (sys.lockup) break;
        bladeBlast(now, 70 + (hash8((uint16_t)now) % 160));
        audioAdvertRandom(ADV_BLASTER, 4);
        trainDeflect(now);
        combo(now);
        break;
      case BE_HOLD:
        if (!sys.lockup) startLockup(now, motion.pitch < -45 ? LOCK_DRAG : LOCK_CLASH);
        break;
      case BE_HOLD_END:
        if (sys.lockup && sys.lockup != LOCK_LIGHTNING) endLockup(now, true);
        break;
    }
  }
}

static void onMotion(uint32_t now, uint8_t ev) {
  if (sys.colorWheel) {
    // turn the hilt like a dial: one full roll = one trip round the colour wheel
    int16_t r = motion.rollDps;
    if ((ev & IMU_SAMPLE) && (r > 10 || r < -10)) {
      hueAcc += (uint16_t)(((int32_t)r * motion.dtMs * 47) >> 9);
      uint8_t h = (uint8_t)(hueAcc >> 7);
      if ((h >> 4) != (sys.hue >> 4) && now - lastWheelTick > 90) {
        lastWheelTick = now;
        audioAdvert(ADV_TICK);
      }
      sys.hue = h;
    }
    return;
  }
  bool settled = bladeSettled();
  if (ev & IMU_STAB) {
    if (sys.lockup == LOCK_CLASH || sys.lockup == LOCK_DRAG) {
      startLockup(now, LOCK_MELT);
    } else if (!sys.lockup && settled) {
      bladeStab(now);
      audioAdvert(ADV_STAB);
      combo(now);
    }
  }
  if (ev & IMU_CLASH) {
    bladeClash(now);
    uiFlash(now);
    if (!sys.lockup || sys.lockup == LOCK_LIGHTNING) audioAdvertRandom(ADV_CLASH, 4);
    cfg.clashes++;
    combo(now);
  }
  if ((ev & IMU_SWING) && settled && !sys.lockup && !(ev & IMU_CLASH)) {
    audioAdvertRandom(motion.swingPeak > SWING_FAST_DPS ? ADV_SWING_FAST : ADV_SWING_SLOW, 4);
    combo(now);
  }
  if (ev & IMU_SWING) trainDeflect(now);
  if ((ev & IMU_TWIST) && (cfg.gestures & GEST_TWIST_OFF) && !sys.lockup && !sys.training &&
      now - onSince > 2000) {
    retract(now);
    return;
  }
  // blade pointing down during a lockup becomes a drag, and back
  if (sys.lockup == LOCK_CLASH && motion.pitch < -50) startLockup(now, LOCK_DRAG);
  else if (sys.lockup == LOCK_DRAG && motion.pitch > -30) startLockup(now, LOCK_CLASH);
}

static void offMotion(uint32_t now, uint8_t ev) {
  if (now - retractAt < 1500 || sys.mode != MODE_OFF) return; // don't re-ignite on the retract flourish
  uint8_t g = cfg.gestures;
  if (((ev & IMU_SWING) && (g & GEST_SWING_ON) && motion.swingPeak > SWING_FAST_DPS) ||
      ((ev & IMU_TWIST) && (g & GEST_TWIST_ON)) || ((ev & IMU_STAB) && (g & GEST_STAB_ON))) {
    ignite(now, false);
  }
}

// ---------------------------------------------------------------- main update

void saberInit(uint32_t now) {
  sys.mode = MODE_BOOT;
  sys.bootAt = now;
  sys.audio = AUDIO_UNKNOWN;
  lastInput = now;
  loadPresetHue();
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
    if (sys.mode == MODE_SLEEP) {
      if (e.type == BE_PRESS) wake(now);
      break;
    }
    if (e.type == BE_PRESS) continue;
    switch (sys.mode) {
      case MODE_OFF: offButton(now, e); break;
      case MODE_ON: onButton(now, e); break;
      case MODE_MENU: menuButton(now, e); break;
    }
  }

  // ---- motion
  switch (sys.mode) {
    case MODE_ON: onMotion(now, ev); break;
    case MODE_OFF: offMotion(now, ev); break;
    case MODE_SLEEP:
      if ((ev & IMU_MOVED) && motion.swingDps > 90) wake(now);
      break;
    case MODE_MENU: menuUpdate(now); break;
  }

  // ---- per-mode timers
  if (isOn()) {
    audioSwell(sys.lockup ? 0 : motion.swing);
    trainUpdate(now);
    if (motion.swingDps > sys.sessPeak) sys.sessPeak = motion.swingDps;
    if (now - motion.lastMotion > IDLE_OFF_MS && !sys.training) retract(now);
  } else if (sys.mode == MODE_OFF) {
    uint8_t mins = pgm_read_byte(&SLEEP_MINUTES[cfg.sleepIdx]);
    uint32_t idle = now - (motion.lastMotion > lastInput ? motion.lastMotion : lastInput);
    if (mins && idle > (uint32_t)mins * 60000UL) goSleep(now);
  }
  if (stopAudioAt && (int32_t)(now - stopAudioAt) >= 0) {
    stopAudioAt = 0;
    if (!isOn()) {
      audioStop();
      audioMute(false);
      sys.muted = false;
    }
  }

  // ---- telemetry history (for the HUD strip chart)
  if (now - lastHist >= 60) {
    lastHist = now;
    sys.histHead = (sys.histHead + 1) % HIST_LEN;
    uint16_t v = isOn() ? motion.swingDps >> 3 : 0;
    sys.hist[sys.histHead] = v > 255 ? 255 : v;
  }

  // ---- battery
  if (sys.battLow && !lowWarned) {
    lowWarned = true;
    uiToast(T_LOWBAT, -1, 2000);
    if (!isOn() && sys.mode != MODE_SLEEP) audioPlay(SND_LOWBATT);
  } else if (!sys.battLow) {
    lowWarned = false;
  }
  if (powerCritical() && !cutoffHandled) {
    cutoffHandled = true;
    if (isOn()) retract(now);
    uiToast(T_EMPTY, -1, 2500);
  } else if (!powerCritical()) {
    cutoffHandled = false;
  }
}
