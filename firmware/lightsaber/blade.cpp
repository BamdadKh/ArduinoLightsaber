#include "blade.h"
#include "state.h"
#include "settings.h"
#include "imu.h"
#include "mathx.h"

CRGB leds[NUM_LEDS];

enum Phase : uint8_t { PH_OFF, PH_IGNITE, PH_ON, PH_RETRACT };

static uint8_t phase;
static uint32_t phaseAt;
static uint16_t phaseDur;
static bool dark = true;
static uint32_t lastFrame;
static uint16_t flowT;   // animation clock; runs faster while the blade is swung
static uint8_t frame;
static uint8_t clashPos, lockPos;
static bool lastLockup;

#define IGNITE_MS 950   // the ignite sound is ~1.4 s; the blade finishes as its whoosh peaks
#define RETRACT_MS 650
static const uint8_t BRIGHT_LUT[10] PROGMEM = {18, 30, 45, 64, 88, 115, 145, 180, 215, 255};

#define N NUM_LEDS

static inline uint8_t sc8(uint8_t a, uint8_t b) { return ((uint16_t)a * (b + 1)) >> 8; }
static NOINLINE void scaleC(CRGB& c, uint8_t v) {
  c.r = sc8(c.r, v);
  c.g = sc8(c.g, v);
  c.b = sc8(c.b, v);
}
static NOINLINE void blendC(CRGB& c, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  c.r = lerp8(c.r, r, a);
  c.g = lerp8(c.g, g, a);
  c.b = lerp8(c.b, b, a);
}
static inline uint8_t easeOut(uint8_t p) { return 255 - (((uint16_t)(255 - p) * (255 - p)) >> 8); }
static inline uint8_t easeIn(uint8_t p) { return ((uint16_t)p * p) >> 8; }
static inline uint8_t dist8(uint8_t a, uint8_t b) { return a > b ? a - b : b - a; }
static inline uint16_t ageOf(uint32_t now, uint32_t at) {
  uint32_t d = now - at;
  return d > 65535 ? 65535 : (uint16_t)d;
}

static uint8_t brightness = 255;

void bladeInit() {
#ifdef ARDUINO
  ledShow(leds, N, 0, LED_MAX_MA);
#endif
}

void bladeSetBrightness(uint8_t level) {
  if (level < 1) level = 1;
  if (level > 10) level = 10;
  brightness = pgm_read_byte(&BRIGHT_LUT[level - 1]);
}

uint8_t bladeBrightness() { return brightness; }

void bladeIgnite(uint32_t now, bool quick) {
  phaseDur = quick ? IGNITE_MS / 2 : IGNITE_MS;
  phaseAt = now;
  phase = PH_IGNITE;
}

void bladeRetract(uint32_t now) {
  if (phase == PH_OFF) return;
  phaseDur = RETRACT_MS;
  phaseAt = now;
  phase = PH_RETRACT;
}

bool bladeSettled() { return phase == PH_ON || phase == PH_OFF; }

void bladeClash(uint32_t now) {
  sys.clashAt = now;
  clashPos = (uint8_t)(((uint16_t)N * (140 + (hash8((uint16_t)now) & 100))) >> 8);
}
void bladeBlast(uint32_t now, uint8_t pos) {
  sys.blastAt = now;
  sys.blastPos = pos;
}
bool bladeRender(uint32_t now) {
  uint16_t dt = ageOf(now, lastFrame);
  if (dt < 1000 / BLADE_FPS_CAP) return false;
  lastFrame = now;
  frame++;
  uint8_t sw = motion.swing;
  flowT += (uint16_t)(((uint32_t)dt * (256 + sw * 3)) >> 8);
  uint16_t t = flowT;

  // --- phase progression
  uint16_t age = ageOf(now, phaseAt);
  uint8_t p = 255;
  if (phase == PH_IGNITE || phase == PH_RETRACT) {
    if (age >= phaseDur) {
      phase = phase == PH_IGNITE ? PH_ON : PH_OFF;
    } else {
      p = (uint32_t)age * 255 / phaseDur;
    }
  }

  if (phase == PH_OFF) {
    sys.ext = 0;
    if (dark) return false;
    for (uint8_t i = 0; i < N; i++) leds[i] = CRGB(0, 0, 0);
    dark = true;
    return true;
  }
  dark = false;

  // --- coverage: which part of the blade is extended this frame
  uint16_t tipQ = (uint16_t)N << 8;  // extension in 1/256 LED
  uint8_t tipGlow = 0;
  if (phase == PH_IGNITE) {
    uint8_t e = easeOut(p);
    tipQ = (uint32_t)e * N;
    tipGlow = 200;
    sys.ext = e;
  } else if (phase == PH_RETRACT) {
    uint8_t e = 255 - easeIn(p);
    tipQ = (uint32_t)e * N;
    tipGlow = 120;
    sys.ext = e;
  } else {
    sys.ext = 255;
  }
  uint8_t tipIdx = tipQ >> 8, tipFrac = tipQ & 0xFF;

  // --- per-frame colours and effect ages
  uint8_t pi = cfg.preset;
  uint8_t sat = presetSat(pi);
  uint8_t hue = presetHue(pi);
  CRGB c1 = hsv(hue, sat, 255);
  uint8_t breathe = (tsin8((uint8_t)(t >> 4)) >> 4) + (vnoise(0, t >> 1) >> 5);  // slow, 0-22

  uint16_t aClash = ageOf(now, sys.clashAt), aBlast = ageOf(now, sys.blastAt);
  bool fxClash = sys.clashAt && aClash < 240;
  bool fxBlast = sys.blastAt && aBlast < 380;
  uint8_t clashA = fxClash ? (uint16_t)(240 - aClash) * (240 - aClash) / 240 : 0;
  uint8_t blastR = 2 + aBlast / 22;
  uint8_t blastFade = fxBlast ? 255 - (uint32_t)aBlast * 255 / 380 : 0;
  uint16_t blastStep = (uint16_t)blastFade * 255 / (blastR + 1);
  uint8_t blastPos = (uint16_t)sys.blastPos * N >> 8;
  if (sys.lockup != lastLockup) {
    lastLockup = sys.lockup;
    lockPos = (uint8_t)(((uint16_t)N * (150 + (hash8((uint16_t)now) & 70))) >> 8);
  }
  uint8_t lockR = 8;
  uint8_t tipZone = N - N / 3;

  for (uint8_t i = 0; i < N; i++) {
    // ---- coverage
    uint8_t cov = i < tipIdx ? 255 : i == tipIdx ? tipFrac : 0;
    if (!cov) {
      leds[i] = CRGB(0, 0, 0);
      continue;
    }

    // ---- base colour: full saturation, slow shimmer along the blade, slight breathing overall.
    // The dips stay shallow because an LED can't go above full, so the blade sits just below it.
    CRGB c = c1;
    scaleC(c, 238 + (vnoise((uint16_t)(i * 24), t) >> 5) + (breathe >> 1));

    // ---- motion: swinging heats the blade, most at the fast-moving tip
    if (sw) {
      uint8_t a = sw >> 4;
      if (i > tipZone) a += (uint16_t)sw * (i - tipZone) >> 8;
      blendC(c, 255, 255, 255, a);
    }

    // ---- ignition tip flare
    if (tipGlow) {
      uint8_t d = tipIdx > i ? tipIdx - i : 0;
      if (d < 5) blendC(c, 255, 255, 255, tipGlow - d * (tipGlow / 5));
    }

    // ---- lockups
    if (sys.lockup) {
      scaleC(c, 200 + (hash8((uint16_t)(frame * 3)) & 55));
      uint8_t d = dist8(i, lockPos);
      if (d < lockR) blendC(c, 255, 255, 230, (uint16_t)(lockR - d) * (150 + (hash8(i, frame) & 105)) >> 3);
    }

    // ---- transient effects
    if (fxClash) {
      uint8_t d = dist8(i, clashPos);
      uint8_t a = (uint16_t)clashA * 3 / 5;
      if (d < 22) {
        uint8_t loc = (22 - d) * 11;
        if (loc > clashA) loc = clashA;
        if (loc > a) a = loc;
      }
      blendC(c, 255, 255, 255, a);
    }
    if (fxBlast) {
      uint8_t d = dist8(i, blastPos);
      if (d <= blastR) blendC(c, 255, 255, 200, (uint16_t)(blastR + 1 - d) * blastStep >> 8);
    }
    if (cov != 255) scaleC(c, cov);
    leds[i] = c;
  }
  return true;
}
