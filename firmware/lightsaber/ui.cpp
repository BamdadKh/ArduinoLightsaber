#include "ui.h"
#include "gfx.h"
#include "state.h"
#include "settings.h"
#include "imu.h"
#include "audio.h"
#include "mathx.h"
#include "config.h"
#ifdef ARDUINO
#include "oled.h"
#endif

enum Screen : uint8_t { SCR_BOOT, SCR_SABER, SCR_IDLE, SCR_HUD, SCR_MENU, SCR_COLOR, SCR_TRAIN };

#define UI_ROWS_PER_PASS 16
#define UI_MIN_FRAME_MS 33
#define BOOT_MS 1900

#define SABER_TOP 10
#define SABER_BOT 89
#define SABER_LEN (SABER_BOT - SABER_TOP + 1)
#define SABER_MID ((SABER_TOP + SABER_BOT) / 2)
#define HILT_Y 91
#define NAME_Y 121

// ---------------------------------------------------------------- static text

static const char MENU_TITLES[MI_COUNT][10] PROGMEM = {
  "SOUND", "LIGHT", "STYLE", "COLOR", "IGNITION", "SWING", "CLASH", "GESTURE",
  "BOOST", "SLEEP", "FLIP", "TRAIN", "CALIBRATE", "STATS", "RESET", "EXIT",
};
static const uint8_t MENU_ICONS[MI_COUNT] PROGMEM = {
  ICON_VOLUME, ICON_BRIGHT, ICON_STYLE, ICON_COLOR, ICON_IGNITE, ICON_SWING, ICON_CLASH, ICON_GESTURE,
  ICON_BOOST, ICON_SLEEP, ICON_FLIP, ICON_TRAIN, ICON_CALIB, ICON_STATS, ICON_RESET, ICON_EXIT,
};
static const char GESTURE_NAMES[GESTURE_OPTIONS][7] PROGMEM = {"OFF", "SWING", "TWIST", "STAB", "ALL"};
static const char BOOST_NAMES[4][5] PROGMEM = {"OFF", "LOW", "MED", "HIGH"};
static const char S_NEVER[] PROGMEM = "NEVER";
static const char S_NORMAL[] PROGMEM = "UP";
static const char S_FLIPPED[] PROGMEM = "DOWN";
static const char S_BLADE_UP[] PROGMEM = "POINT BLADE UP, PRESS M";
static const char S_HOLD_M[] PROGMEM = "HOLD M";
static const char S_BACK[] PROGMEM = "BACK";
static const char S_CUSTOM[] PROGMEM = "CUSTOM";
static const char S_MSET[] PROGMEM = "M:SET";
static const char S_MGO[] PROGMEM = "M:GO";
static const char S_ANXT[] PROGMEM = "A:NXT";
static const char S_MOK[] PROGMEM = "M:OK";
static const char S_AESC[] PROGMEM = "A:ESC";
static const char S_MEND[] PROGMEM = "M:END";
static const char S_HUE[] PROGMEM = "HUE";
static const char S_COLOR[] PROGMEM = "COLOR";
static const char S_TRAIN[] PROGMEM = "TRAIN";
static const char S_READY[] PROGMEM = "READY";
static const char S_GAME[] PROGMEM = "GAME";
static const char S_OVER[] PROGMEM = "OVER";
static const char S_BEST[] PROGMEM = "BEST";
static const char S_HIT[] PROGMEM = "HIT!";
static const char S_KYBER[] PROGMEM = "KYBER";
static const char S_OS[] PROGMEM = "OS2.0";
static const char S_DPS[] PROGMEM = "DPS";
static const char S_DIAG[4][4] PROGMEM = {"IMU", "SND", "SD", "BAT"};

// Fixed labels per screen: {screen, y, text}
struct Label {
  uint8_t scr, y;
  const char* s;
};
static const Label LABELS[] PROGMEM = {
  {SCR_HUD, 11, S_DPS},
  {SCR_MENU, 90, S_ANXT},
  {SCR_COLOR, 0, S_COLOR}, {SCR_COLOR, 50, S_HUE}, {SCR_COLOR, 98, S_MOK}, {SCR_COLOR, 108, S_AESC},
  {SCR_TRAIN, 0, S_TRAIN}, {SCR_TRAIN, 120, S_MEND},
};

// 5x5 glyphs, MSB = left
static const uint8_t HEART[5] PROGMEM = {0x50, 0xF8, 0xF8, 0x70, 0x20};
static const uint8_t HEART_O[5] PROGMEM = {0x50, 0xA8, 0x88, 0x50, 0x20};
static const uint8_t CHECK[5] PROGMEM = {0x08, 0x10, 0xA0, 0x40, 0x00};
static const uint8_t CROSS[5] PROGMEM = {0x88, 0x50, 0x20, 0x50, 0x88};

// ---------------------------------------------------------------- frame snapshot

// Per-frame text: centred (or marquee) strings, from flash or built in RAM.
struct Txt {
  uint8_t y, scale;
  const char* p;   // flash string, or null -> buf
  char buf[12];
};
#define MAX_TXT 8
static Txt txt[MAX_TXT];
static uint8_t nTxt;

static uint8_t scr;
static uint32_t fT;       // ms clock of this frame (for event ages)
static uint16_t fa;       // same, 16-bit, for animation phases
static uint8_t fN;        // frame counter
static bool blink;        // ~2 Hz
static uint8_t battFill;  // 0-7 px
static char pctBuf[5];
static bool quiet;        // no sound: muted or no DFPlayer/SD
static int16_t valFrac;   // menu bar 0-255, or -1
static uint8_t bladeTop;
static int8_t pitchY;
static bool toastOn;
static char toastBuf[16];
static uint8_t style, preset;
static int8_t cux, cuy;   // colour wheel pointer, +-127
static char statBuf[72];
static bool statOn;

// "LABEL 123  " -> returns chars written
static __attribute__((unused)) uint8_t appendStat(char* b, const char* labelP, uint16_t v) {
  uint8_t n = strlen_P(labelP);
  memcpy_P(b, labelP, n);
  n += fmtNum(b + n, v);
  b[n++] = ' ';
  b[n++] = ' ';
  b[n] = 0;
  return n;
}

static uint8_t row = SCR_H;
static uint32_t frameAt;
static bool inverted;
static uint32_t invertUntil;

static Txt* addP(uint8_t y, const char* p, uint8_t scale = 1) {
  if (nTxt >= MAX_TXT) return &txt[MAX_TXT - 1];
  Txt* t = &txt[nTxt++];
  t->y = y;
  t->scale = scale;
  t->p = p;
  t->buf[0] = 0;
  return t;
}
static char* addBuf(uint8_t y, uint8_t scale = 1) { return addP(y, nullptr, scale)->buf; }
static void addNum(uint8_t y, uint16_t v, uint8_t scale = 1) { fmtNum(addBuf(y, scale), v); }

// "X 123": label letter + number, 5 characters (= screen width)
static void labelNum(char* b, char label, uint16_t v) {
  char n[6];
  uint8_t len = fmtNum(n, v);
  b[0] = label;
  uint8_t i = 1;
  while (i + len < 5) b[i++] = ' ';
  memcpy(b + i, n, len + 1);
}

static uint8_t dist(uint8_t a, uint8_t b) { return a > b ? a - b : b - a; }

static Row statusRow(uint8_t y) {
  if (y == 8) return 0xAAAAAAAAUL;
  if (y > 6) return 0;
  Row r = 0;
  if (!sys.battLow || blink) {
    if (y == 0 || y == 6) r = span(0, 10);
    else {
      r = pix(0) | pix(10);
      if (y >= 2 && y <= 4) r |= pix(11) | (battFill ? span(2, 1 + battFill) : 0);
    }
  }
  if (quiet) r |= bmpRow(NOSOUND_BMP, 1, NOSOUND_W, NOSOUND_H, 13, 1, y);
  if (sys.battPresent) r |= textRow(pctBuf, false, 32 - textWidth(strLen(pctBuf, false), 1), 0, y);
  return r;
}

static Row randMask(uint8_t y, uint8_t salt) {
  uint8_t s = fN + salt;
  return (uint32_t)hash8(y, s) << 24 | (uint32_t)hash8(y + 40, s) << 16 | (uint16_t)hash8(y + 80, s) << 8 |
         hash8(y + 120, s);
}

// ---------------------------------------------------------------- saber screen

// Halo levels (0-16) of the mini blade for row y, mimicking the blade style.
static void glowFor(uint8_t y, uint8_t& l1, uint8_t& l2) {
  uint8_t w;
  switch (style) {
    case STYLE_UNSTABLE: w = hash8(y, fN); break;
    case STYLE_PULSE: w = tsin8((uint8_t)((fa >> 2) - y * 6)); break;
    case STYLE_FIRE: w = vnoise((uint16_t)(y * 48 + (fa >> 1)), fa); break;
    case STYLE_RAINBOW: w = (uint8_t)((y + (fa >> 5)) << 4); break;
    case STYLE_PLASMA: w = vnoise((uint16_t)(y * 50), fa * 3); break;
    case STYLE_CANDY: w = ((y * 2 + (fa >> 4)) & 16) ? 230 : 60; break;
    case STYLE_FLOW: w = tsin8((uint8_t)(y * 7 + (fa >> 2))) > 200 ? 255 : 90; break;
    default: w = 150 + (hash8(y >> 2, fN >> 1) & 48); break;
  }
  l1 = 2 + (w >> 4) * 7 / 8;
  l2 = w >> 6;
}

static Row miniBlade(uint8_t y) {
  if (y < bladeTop) return 0;
  uint8_t d = y - bladeTop;
  uint8_t l1, l2, l3 = motion.swing / 20;
  glowFor(y, l1, l2);
  l2 += motion.swing >> 5;
  if (sys.clashAt && fT - sys.clashAt < 200) l1 = 16, l2 = 12, l3 = 8;
  Row r = (d == 0 ? span(15, 16) : span(14, 17)) | (dither(l1, y) & 0x000C3000UL) |
          (dither(l2, y) & 0x00300C00UL) | (dither(l3, y) & 0x00C00300UL);
  if (d == 0) r &= span(12, 19);

  uint8_t ly = SABER_BOT - SABER_LEN * 2 / 3;
  switch (sys.lockup) {
    case LOCK_CLASH: {
      uint8_t dy = dist(y, ly);
      if (dy < 8) r |= randMask(y, 0) & randMask(y, 7) & span(3 + dy, 28 - dy);
      break;
    }
    case LOCK_DRAG:
      if (d < 7) r |= randMask(y, 0) & randMask(y, 3) & span(6, 25);
      break;
    case LOCK_MELT:
      if (d < 5) r |= span(10, 21);
      else if (d < 12) r |= randMask(y, 1) & randMask(y, 9) & span(9, 22);
      break;
    case LOCK_LIGHTNING: {
      uint8_t h = hash8(y >> 2, fN);
      r |= pix(5 + (h & 3)) | pix(26 - ((h >> 2) & 3));
      if ((h >> 4) > 12) r |= span(4, 27) & randMask(y, 5);
      break;
    }
  }
  uint16_t aBlast = fT - sys.blastAt;
  if (sys.blastAt && aBlast < 320) {
    uint8_t by = SABER_BOT - ((uint16_t)sys.blastPos * SABER_LEN >> 8);
    uint8_t rr = 2 + aBlast / 40, dy = dist(y, by);
    if (dy <= rr) r |= pix(11 - rr + dy) | pix(20 + rr - dy);
  }
  uint16_t aForce = fT - sys.forceAt;
  if (sys.forceAt && aForce < 600) {
    uint8_t wy = SABER_BOT - (uint16_t)aForce * SABER_LEN / 600;
    if (dist(y, wy) < 2) r |= span(2, 29) & dither(8, y);
  }
  return r;
}

static Row saberSide(uint8_t y) {
  Row r = 0;
  uint8_t k = y - SABER_TOP;
  // left: swing meter with a ruler
  if (motion.swing && y >= SABER_BOT - (uint16_t)motion.swing * (SABER_LEN - 1) / 255) r |= span(1, 2);
  if ((k & 7) == 0) r |= pix(0);
  // right: pitch ladder and marker
  if (k % 10 == 0) r |= pix(31);
  if (y == SABER_MID) r |= span(29, 31);
  uint8_t a = dist(y, pitchY);
  if (a <= 2) r |= span(26 + a, 28);
  // training: incoming bolt marker
  if (sys.training && sys.trainPhase == TRAIN_INCOMING && blink) {
    uint8_t e = dist(y, SABER_BOT - ((uint16_t)sys.trainPos * SABER_LEN >> 8));
    if (e <= 3) r |= span(23 + e, 26);
  }
  return r;
}

static Row saberIdle(uint8_t y) {
  Row r = 0;
  if (y >= 31 && y <= 33) {
    for (uint8_t k = 0; k < NUM_PRESETS; k++)
      if (k == preset || y == 32) r |= span(k * 4 + 1, k * 4 + 2);
  }
  // hue scale with a marker
  if (y >= 62 && y <= 68) {
    uint8_t mx = 2 + (uint16_t)sys.hue * 27 / 255;
    uint8_t a = dist(y, 65);
    if (y == 65) r |= span(1, 30) & 0xAAAAAAAAUL;
    if (a <= 3) r |= span(mx - (3 - a) / 2, mx + (3 - a) / 2);
    if (a == 3) r |= pix(0) | pix(31);
  }
  // a faint ghost of the blade, breathing
  if (y >= 74 && y <= SABER_BOT) r |= dither(1 + (tsin8((uint8_t)(fa >> 4)) >> 6) + (y > 82 ? 2 : 0), y) & span(14, 17);
  return r;
}

static Row rowSaber(uint8_t y) {
  if (y <= 8) return statusRow(y);
  if (y >= HILT_Y) {
    Row r = bmpRow(HILT_BMP, 2, HILT_W, HILT_H, 10, HILT_Y, y);
    if (y == HILT_Y + HILT_CRYSTAL_ROW + 1) {
      // kyber crystal: slow heartbeat when off, alive and flickering when lit
      bool lit = sys.ext ? (motion.swing < 40 || (fN & 1)) : tsin8((uint8_t)(fa >> 3)) > 170;
      if (lit) r |= span(15, 16);
    }
    return r;
  }
  if (y > SABER_BOT || y < SABER_TOP) return 0;
  if (scr == SCR_IDLE) return saberIdle(y);
  return miniBlade(y) | saberSide(y);
}

// ---------------------------------------------------------------- telemetry HUD

static Row rowHud(uint8_t y) {
  if (y <= 8) return statusRow(y);
  Row r = 0;
  // scrolling strip chart, newest sample at the top, mirrored like a waveform
  if (y >= 30 && y < 118) {
    uint8_t k = (y - 30) >> 1;
    uint8_t w = sys.hist[(uint8_t)(sys.histHead + HIST_LEN - k) % HIST_LEN] >> 4;
    if (w) r |= span(16 - w, 15 + w);
    if (((y - 30) & 15) == 0) r |= pix(0) | pix(31);
  }
  return r;
}

// ---------------------------------------------------------------- menu

static Row rowMenu(uint8_t y) {
  if (y == 8 || y == 126) return 0xAAAAAAAAUL;
  uint8_t item = sys.menuItem;
  Row r = iconRow(pgm_read_byte(&MENU_ICONS[item]), 0, 11, y, 2);
  if (valFrac >= 0 && y >= 66 && y <= 71) {
    if (y == 66 || y == 71) r |= 0xFFFFFFFFUL;
    else r |= pix(0) | pix(31) | (valFrac ? span(2, 2 + (valFrac * 27 >> 8)) : 0);
  }
  if (y == 125) r |= span(item * 2, item * 2 + 1);
  return r;
}

// ---------------------------------------------------------------- colour wheel

static Row rowColor(uint8_t y) {
  Row r = iconRow(ICON_GESTURE, 8, 72, y, 1);
  if (y >= 77 && y < 84) {
    // arrows sway around the twist icon
    int8_t wob = (int8_t)((tsin8((uint8_t)(fa >> 2)) - 128) >> 5);
    r |= textRow(PSTR("<"), true, -wob, 77, y) | textRow(PSTR(">"), true, 27 + wob, 77, y);
  }
  if (y < 15 || y > 45) return r;
  int8_t Y2 = 2 * y - 60;
  for (uint8_t x = 0; x < 32; x++) {
    int8_t X2 = 2 * x - 31;
    int16_t r2 = X2 * X2 + Y2 * Y2;
    bool on = (r2 >= 529 && r2 <= 718) || r2 <= 12; // ring and hub
    if (!on) {
      int16_t along = X2 * cux + Y2 * cuy;
      int16_t perp = X2 * cuy - Y2 * cux;
      on = along > 0 && along < 19 * 127 && perp > -150 && perp < 150;
      int8_t tx = X2 - cux * 19 / 127, ty = Y2 - cuy * 19 / 127;
      on |= tx * tx + ty * ty <= 20;
    }
    if (on) r |= pix(x);
  }
  return r;
}

// ---------------------------------------------------------------- training droid

static Row rowTrain(uint8_t y) {
  Row r = 0;
  if (y >= 9 && y <= 13)
    for (uint8_t i = 0; i < 3; i++) r |= bmpRow(i < sys.trainLives ? HEART : HEART_O, 1, 5, 5, 3 + i * 10, 9, y);
  if (sys.trainPhase == TRAIN_OVER) return r;
  uint16_t age = fT - sys.trainAt;
  r |= iconRow(ICON_TRAIN, 8 + ((int8_t)(tsin8((uint8_t)(fa / 5)) - 128) >> 5), 36, y, 1);
  if (sys.trainPhase == TRAIN_INCOMING) {
    uint16_t w = sys.trainWindow | 1;
    uint8_t head = 52 + (uint32_t)(age > w ? w : age) * 46 / w;
    if (y + 8 >= head && y <= head && !((y + fN) & 2)) r |= span(15, 16);
  } else if (sys.trainPhase == TRAIN_RESULT) {
    if (sys.trainHit) {
      if (y >= 66 && y <= 80 && blink) r ^= 0xFFFFFFFFUL;
    } else if (age < 400) {
      uint8_t head = 98 - (uint32_t)age * 46 / 400;
      if (y + 6 >= head && y <= head) r |= span(15 + (98 - y) / 6, 16 + (98 - y) / 6);
    }
  }
  if (y >= 99 && y <= 101) r |= span(9, 22); // your blade's guard
  return r;
}

// ---------------------------------------------------------------- boot

static Row rowBoot(uint8_t y) {
  uint16_t t = fT - sys.bootAt;
  Row r = 0;
  if (t < 256 && y == t >> 1) return 0xFFFFFFFFUL; // CRT-style power-on sweep
  for (uint8_t k = 0; k < 4; k++) {
    uint8_t y0 = 28 + k * 10;
    if (t < 700u + k * 160u || y < y0 || y >= y0 + 7) continue;
    r |= textRow(S_DIAG[k], true, 0, y0, y);
    uint8_t st; // 0 ok, 1 fail, 2 pending
    bool pend = sys.audio == AUDIO_UNKNOWN;
    switch (k) {
      case 0: st = !sys.imuOk; break;
      case 1: st = pend ? 2 : sys.audio == AUDIO_NO_MODULE; break;
      case 2: st = pend ? 2 : sys.audio != AUDIO_OK; break;
      default: st = !sys.battPresent; break;
    }
    if (st == 2) {
      if (y == y0 + 3) r |= pix(25 + ((fN >> 1) & 3) * 2);
    } else {
      r |= bmpRow(st ? CROSS : CHECK, 1, 5, 5, 26, y0 + 1, y);
    }
  }
  if (y >= 72 && y <= 76) {
    uint8_t fill = t >= BOOT_MS ? 29 : (uint32_t)t * 29 / BOOT_MS;
    r |= (y == 72 || y == 76) ? 0xFFFFFFFFUL : pix(0) | pix(31) | (fill ? span(2, 1 + fill) : 0);
  }
  if (y >= 94) r |= bmpRow(HILT_BMP, 2, HILT_W, HILT_H, 10, 94, y);
  else if (t > 1300 && y >= 80 && y + (t - 1300) / 25 >= 93) r |= span(14, 17); // blade ignites
  if (y == 22 && t > 600) r |= 0xAAAAAAAAUL;
  return r;
}

// ---------------------------------------------------------------- frame setup

static void menuTexts() {
  uint8_t item = sys.menuItem;
  addP(0, MENU_TITLES[item]);
  addP(80, item >= MI_TRAIN ? S_MGO : S_MSET);
  char* pg = addBuf(110);
  uint8_t n = fmtNum(pg, item + 1);
  pg[n++] = '/';
  fmtNum(pg + n, MI_COUNT);

  uint8_t num = 0;
  const char* v = nullptr;
  switch (item) {
    case MI_VOLUME: num = cfg.volume; valFrac = cfg.volume * 255 / 30; break;
    case MI_BRIGHT: num = cfg.bright; valFrac = cfg.bright * 255 / 10; break;
    case MI_SWING: num = cfg.swingSens; valFrac = cfg.swingSens * 255 / 9; break;
    case MI_CLASH: num = cfg.clashSens; valFrac = cfg.clashSens * 255 / 9; break;
    case MI_COLOR: valFrac = sys.hue; addNum(51, (uint16_t)sys.hue * 45 >> 5); return;
    case MI_STYLE: v = STYLE_NAMES[style]; break;
    case MI_IGNITE: v = IGNITION_NAMES[cfg.ignition[preset]]; break;
    case MI_GESTURE:
      v = S_CUSTOM;
      for (uint8_t i = 0; i < GESTURE_OPTIONS; i++)
        if (GESTURE_MASKS[i] == cfg.gestures) v = GESTURE_NAMES[i];
      break;
    case MI_BOOST: v = BOOST_NAMES[cfg.boost & 3]; valFrac = cfg.boost * 85; break;
    case MI_SLEEP: {
      uint8_t m = pgm_read_byte(&SLEEP_MINUTES[cfg.sleepIdx]);
      if (!m) v = S_NEVER;
      else {
        char* b = addBuf(51);
        memcpy_P(b + fmtNum(b, m), PSTR(" MIN"), 5);
      }
      break;
    }
    case MI_FLIP: v = cfg.flip ? S_FLIPPED : S_NORMAL; break;
    case MI_TRAIN: {
      char* b = addBuf(51);
      memcpy_P(b, S_BEST, 4);
      b[4] = ' ';
      fmtNum(b + 5, cfg.trainBest);
      break;
    }
    case MI_CALIB: v = S_BLADE_UP; break;
    case MI_STATS: {
#if !DEBUG_SERIAL // (debug builds are short on flash; the serial telemetry covers it)
      // lifetime stats as a scrolling ticker
      char* b = statBuf;
      b += appendStat(b, PSTR("BATT MV "), sys.battMv);
      b += appendStat(b, PSTR("TEMP "), motion.tempC < 0 ? 0 : motion.tempC);
      b += appendStat(b, PSTR("IGNITIONS "), cfg.ignitions);
      b += appendStat(b, PSTR("CLASHES "), cfg.clashes);
      b += appendStat(b, PSTR("PEAK DPS "), cfg.peakDps);
      appendStat(b, PSTR("HOURS "), cfg.onSeconds / 3600);
      statOn = true;
#endif
      break;
    }
    case MI_RESET: v = S_HOLD_M; break;
    case MI_EXIT: v = S_BACK; break;
  }
  if (v) addP(51, v);
  else if (valFrac >= 0) addNum(47, num, 2);
}

void uiBeginFrame(uint32_t now) {
  fT = now;
  fa = (uint16_t)now;
  fN++;
  blink = (now >> 8) & 1;
  preset = cfg.preset;
  style = cfg.style[preset];
  nTxt = 0;
  valFrac = -1;
  statOn = false;

  battFill = sys.battPresent ? (uint16_t)sys.battPct * 7 / 100 : 0;
  {
    quiet = sys.muted || sys.audio == AUDIO_NO_MODULE || sys.audio == AUDIO_NO_CARD;
    uint8_t p = quiet && sys.battPct > 99 ? 99 : sys.battPct;
    uint8_t n = fmtNum(pctBuf, p);
    if (n < 3 && !quiet) {
      pctBuf[n] = '%';
      pctBuf[n + 1] = 0;
    }
  }

  switch (sys.mode) {
    case MODE_BOOT: scr = SCR_BOOT; break;
    case MODE_MENU: scr = SCR_MENU; break;
    case MODE_ON: scr = sys.colorWheel ? SCR_COLOR : sys.training ? SCR_TRAIN : sys.hud ? SCR_HUD : SCR_SABER; break;
    default: scr = sys.ext ? SCR_SABER : SCR_IDLE; break;
  }
  bladeTop = SABER_BOT + 1 - (uint16_t)sys.ext * SABER_LEN / 255;
  pitchY = SABER_MID - motion.pitch * 2 / 5;

  switch (scr) {
    case SCR_IDLE:
      addNum(13, preset + 1, 2);
      addP(40, STYLE_NAMES[style]);
      addP(50, IGNITION_NAMES[cfg.ignition[preset]]);
      // fall through
    case SCR_SABER:
      addP(NAME_Y, PRESETS[preset].name);
      if (sys.combo >= 2 && now - sys.comboAt < 1500) {
        char* b = addBuf(SABER_TOP + 1);
        b[0] = 'X';
        fmtNum(b + 1, sys.combo);
      }
      break;
    case SCR_HUD:
      addNum(20, motion.swingDps);
      labelNum(addBuf(121), 'P', sys.sessPeak);
      break;
    case SCR_COLOR:
      addNum(59, (uint16_t)sys.hue * 45 >> 5); // degrees
      cux = (int8_t)(tsin8(sys.hue) - 128);
      cuy = (int8_t)(128 - tsin8((uint8_t)(sys.hue + 64)));
      break;
    case SCR_TRAIN:
      addNum(18, sys.trainScore, 2);
      if (sys.trainPhase == TRAIN_OVER) {
        if (blink) {
          addP(40, S_GAME);
          addP(49, S_OVER);
        }
        addP(64, S_BEST);
        addNum(73, cfg.trainBest, 2);
      } else {
        if (sys.trainPhase == TRAIN_RESULT && sys.trainHit) addP(70, S_HIT);
        else if (sys.trainPhase == TRAIN_WAIT && !sys.trainScore && sys.trainLives == 3 && blink) addP(70, S_READY);
        if (sys.trainReact) {
          char* b = addBuf(106);
          memcpy_P(b + fmtNum(b, sys.trainReact), PSTR("MS"), 3);
        }
      }
      break;
    case SCR_MENU:
      menuTexts();
      break;
    case SCR_BOOT: {
      uint16_t t = now - sys.bootAt;
      if (t > 200) {
        char* b = addBuf(3);
        uint8_t n = (t - 200) / 60;
        memcpy_P(b, S_KYBER, 6);
        if (n < 5) b[n] = 0;
      }
      if (t > 560) addP(12, S_OS);
      break;
    }
  }

  toastOn = sys.toast && (int32_t)(sys.toastUntil - now) > 0;
  if (toastOn) {
    strncpy_P(toastBuf, sys.toast, 11); // + " 100" fits the 16-byte buffer
    toastBuf[11] = 0;
    if (sys.toastNum >= 0) {
      uint8_t n = strlen(toastBuf);
      toastBuf[n++] = ' ';
      fmtNum(toastBuf + n, sys.toastNum);
    }
  }
}

uint32_t uiRow(uint8_t y) {
  if (toastOn && y >= 53 && y <= 71) return (y == 53 || y == 71) ? 0 : ~textFit(toastBuf, false, 59, y, fa);
  Row r;
  switch (scr) {
    case SCR_BOOT: r = rowBoot(y); break;
    case SCR_HUD: r = rowHud(y); break;
    case SCR_MENU: r = rowMenu(y); break;
    case SCR_COLOR: r = rowColor(y); break;
    case SCR_TRAIN: r = rowTrain(y); break;
    default: r = rowSaber(y); break;
  }
  for (uint8_t i = 0; i < sizeof(LABELS) / sizeof(LABELS[0]); i++) {
    if (pgm_read_byte(&LABELS[i].scr) != scr) continue;
    r |= textFit((const char*)pgm_read_ptr(&LABELS[i].s), true, pgm_read_byte(&LABELS[i].y), y, 0);
  }
  for (uint8_t i = 0; i < nTxt; i++) {
    Txt& t = txt[i];
    r |= textFit(t.p ? t.p : t.buf, t.p != nullptr, t.y, y, fa, t.scale);
  }
  if (statOn) r |= textFit(statBuf, false, 51, y, fa);
  if (scr == SCR_MENU && y >= 45 && y <= 62 && fT - sys.menuEditAt < 150) r = ~r; // edit feedback
  return r;
}

// ---------------------------------------------------------------- device side

void uiToast(const char* msgP, int16_t num, uint16_t ms) {
  sys.toast = msgP;
  sys.toastNum = num;
  sys.toastUntil = sys.now + ms;
}

#ifdef ARDUINO
void uiInit() { oledInit(cfg.flip); }

void uiFlash(uint32_t now) {
  if (sys.mode == MODE_SLEEP) return;
  oledInvert(true);
  inverted = true;
  invertUntil = now + 70;
}

void uiSetFlip(bool flip) { oledFlip(flip); }

void uiSleep(bool sleep) {
  oledPower(!sleep);
  row = SCR_H;
}

void uiRedraw() {
  row = SCR_H;
  frameAt = 0;
}

void uiPump(uint32_t now) {
  if (inverted && (int32_t)(now - invertUntil) >= 0) {
    oledInvert(false);
    inverted = false;
  }
  if (sys.mode == MODE_SLEEP) return;
  if (row >= SCR_H) {
    if (now - frameAt < UI_MIN_FRAME_MS) return;
    frameAt = now;
    uiBeginFrame(now);
    oledWindowAll();
    row = 0;
  }
  oledRowsBegin();
  for (uint8_t k = 0; k < UI_ROWS_PER_PASS && row < SCR_H; k++) oledRow(uiRow(row++));
  oledRowsEnd();
}
#else
void uiInit() {}
void uiFlash(uint32_t) {}
void uiSetFlip(bool) {}
void uiSleep(bool) {}
void uiRedraw() {}
void uiPump(uint32_t) {}
#endif
