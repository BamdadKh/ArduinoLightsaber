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

enum Screen : uint8_t { SCR_BOOT, SCR_SABER, SCR_IDLE, SCR_MENU };

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
  "SOUND", "LIGHT", "COLOR", "SENS", "FLIP", "CALIBRATE", "BATTERY", "EXIT",
};
static const uint8_t MENU_ICONS[MI_COUNT] PROGMEM = {
  ICON_VOLUME, ICON_BRIGHT, ICON_COLOR, ICON_SWING, ICON_FLIP, ICON_CALIB, ICON_STATS, ICON_EXIT,
};
static const char S_NORMAL[] PROGMEM = "UP";
static const char S_FLIPPED[] PROGMEM = "DOWN";
static const char S_BLADE_UP[] PROGMEM = "KEEP STILL, PRESS M";
static const char S_BACK[] PROGMEM = "BACK";
static const char S_USB[] PROGMEM = "USB";
static const char S_MSET[] PROGMEM = "M:SET";
static const char S_MGO[] PROGMEM = "M:GO";
static const char S_ANXT[] PROGMEM = "A:NXT";
static const char S_KYBER[] PROGMEM = "KYBER";
static const char S_OS[] PROGMEM = "OS2.0";
static const char S_DIAG[3][4] PROGMEM = {"IMU", "SND", "SD"};

// Fixed labels per screen: {screen, y, text}
struct Label {
  uint8_t scr, y;
  const char* s;
};
static const Label LABELS[] PROGMEM = {
  {SCR_MENU, 90, S_ANXT},
};

// 5x5 glyphs, MSB = left
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
static bool toastOn;
static char toastBuf[16];
static uint8_t preset;

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
  if (sys.onBattery) r |= textRow(pctBuf, false, 32 - textWidth(strLen(pctBuf, false), 1), 0, y);
  return r;
}

static Row randMask(uint8_t y, uint8_t salt) {
  uint8_t s = fN + salt;
  return (uint32_t)hash8(y, s) << 24 | (uint32_t)hash8(y + 40, s) << 16 | (uint16_t)hash8(y + 80, s) << 8 |
         hash8(y + 120, s);
}

// ---------------------------------------------------------------- saber screen

// Halo levels (0-16) of the mini blade for row y.
static void glowFor(uint8_t y, uint8_t& l1, uint8_t& l2) {
  uint8_t w = 170 + (vnoise((uint16_t)(y * 40), fa) >> 4);
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

  if (sys.lockup) {
    uint8_t ly = SABER_BOT - SABER_LEN * 2 / 3;
    uint8_t dy = dist(y, ly);
    if (dy < 8) r |= randMask(y, 0) & randMask(y, 7) & span(3 + dy, 28 - dy);
  }
  uint16_t aBlast = fT - sys.blastAt;
  if (sys.blastAt && aBlast < 320) {
    uint8_t by = SABER_BOT - ((uint16_t)sys.blastPos * SABER_LEN >> 8);
    uint8_t rr = 2 + aBlast / 40, dy = dist(y, by);
    if (dy <= rr) r |= pix(11 - rr + dy) | pix(20 + rr - dy);
  }
  return r;
}

static Row saberSide(uint8_t y) {
  Row r = 0;
  uint8_t k = y - SABER_TOP;
  // left: swing meter with a ruler
  if (motion.swing && y >= SABER_BOT - (uint16_t)motion.swing * (SABER_LEN - 1) / 255) r |= span(1, 2);
  if ((k & 7) == 0) r |= pix(0);
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
    uint8_t mx = 2 + (uint16_t)presetHue(preset) * 27 / 255;
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

// ---------------------------------------------------------------- boot

static Row rowBoot(uint8_t y) {
  uint16_t t = fT - sys.bootAt;
  Row r = 0;
  if (t < 256 && y == t >> 1) return 0xFFFFFFFFUL; // CRT-style power-on sweep
  for (uint8_t k = 0; k < 3; k++) {
    uint8_t y0 = 28 + k * 10;
    if (t < 700u + k * 160u || y < y0 || y >= y0 + 7) continue;
    r |= textRow(S_DIAG[k], true, 0, y0, y);
    uint8_t st; // 0 ok, 1 fail, 2 pending
    bool pend = sys.audio == AUDIO_UNKNOWN;
    switch (k) {
      case 0: st = !sys.imuOk; break;
      case 1: st = pend ? 2 : sys.audio == AUDIO_NO_MODULE; break;
      default: st = pend ? 2 : sys.audio != AUDIO_OK; break;
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
  addP(80, item >= MI_CALIB ? S_MGO : S_MSET);
  char* pg = addBuf(110);
  uint8_t n = fmtNum(pg, item + 1);
  pg[n++] = '/';
  fmtNum(pg + n, MI_COUNT);

  uint8_t num = 0;
  const char* v = nullptr;
  switch (item) {
    case MI_VOLUME: num = cfg.volume; valFrac = cfg.volume * 255 / 30; break;
    case MI_BRIGHT: num = cfg.bright; valFrac = cfg.bright * 255 / 10; break;
    case MI_SENS: num = cfg.sens; valFrac = cfg.sens * 255 / 9; break;
    case MI_COLOR: v = PRESETS[preset].name; break;
    case MI_FLIP: v = cfg.flip ? S_FLIPPED : S_NORMAL; break;
    case MI_CALIB: v = S_BLADE_UP; break;
    case MI_BATT:
      if (sys.onBattery && sys.battMv) addNum(51, sys.battMv);
      else addP(51, S_USB);
      break;
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
  nTxt = 0;
  valFrac = -1;

  battFill = sys.onBattery ? (uint16_t)sys.battPct * 7 / 100 : 0;
  {
    quiet = sys.audio == AUDIO_NO_MODULE || sys.audio == AUDIO_NO_CARD;
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
    case MODE_ON: scr = SCR_SABER; break;
    default: scr = sys.ext ? SCR_SABER : SCR_IDLE; break;
  }
  bladeTop = SABER_BOT + 1 - (uint16_t)sys.ext * SABER_LEN / 255;

  switch (scr) {
    case SCR_IDLE:
      addNum(13, preset + 1, 2);
      // fall through
    case SCR_SABER:
      addP(NAME_Y, PRESETS[preset].name);
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
    case SCR_MENU: r = rowMenu(y); break;
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
  oledInvert(true);
  inverted = true;
  invertUntil = now + 70;
}

void uiSetFlip(bool flip) { oledFlip(flip); }

void uiRedraw() {
  row = SCR_H;
  frameAt = 0;
}

void uiPump(uint32_t now) {
  if (inverted && (int32_t)(now - invertUntil) >= 0) {
    oledInvert(false);
    inverted = false;
  }
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
void uiRedraw() {}
void uiPump(uint32_t) {}
#endif
