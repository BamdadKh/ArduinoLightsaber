#include "blade.h"
#include "state.h"
#include "settings.h"
#include "imu.h"
#include "mathx.h"

CRGB leds[NUM_LEDS];

enum Phase : uint8_t { PH_OFF, PH_IGNITE, PH_ON, PH_RETRACT };

static uint8_t phase;
static uint8_t animStyle;
static uint32_t phaseAt;
static uint16_t phaseDur;
static bool preview;
static bool dark = true;
static uint32_t lastFrame;
static uint16_t flowT;   // animation clock; runs faster while the blade is swung
static uint8_t frame;
static uint32_t meterAt, hitAt;
static uint8_t meterPct;
static uint8_t clashPos, lockPos;
static uint8_t lastLockup;

static const uint16_t IGN_MS[NUM_IGNITIONS] PROGMEM = {380, 460, 900, 640};
static const uint16_t RET_MS[NUM_IGNITIONS] PROGMEM = {480, 560, 800, 760};
static const uint8_t BRIGHT_LUT[10] PROGMEM = {18, 30, 45, 64, 88, 115, 145, 180, 215, 255};

#define N NUM_LEDS
#define METER_MS 2600

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
static void blendC(CRGB& c, const CRGB& t, uint8_t a) { blendC(c, t.r, t.g, t.b, a); }
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
  animStyle = cfg.ignition[cfg.preset];
  phaseDur = pgm_read_word(&IGN_MS[animStyle]);
  if (quick) phaseDur >>= 1;
  phaseAt = now;
  phase = PH_IGNITE;
  preview = false;
}

void bladeRetract(uint32_t now) {
  if (phase == PH_OFF) return;
  animStyle = cfg.ignition[cfg.preset];
  phaseDur = pgm_read_word(&RET_MS[animStyle]);
  phaseAt = now;
  phase = PH_RETRACT;
}

bool bladeLit() { return phase != PH_OFF; }
bool bladeSettled() { return phase == PH_ON || phase == PH_OFF; }
void bladePreview(bool on) { preview = on; }

void bladeClash(uint32_t now) {
  sys.clashAt = now;
  clashPos = (uint8_t)(((uint16_t)N * (140 + (hash8((uint16_t)now) & 100))) >> 8);
}
void bladeBlast(uint32_t now, uint8_t pos) {
  sys.blastAt = now;
  sys.blastPos = pos;
}
void bladeStab(uint32_t now) { sys.stabAt = now; }
void bladeForce(uint32_t now) { sys.forceAt = now; }
void bladeHitFlash(uint32_t now) { hitAt = now; }
void bladeMeter(uint32_t now, uint8_t pct) {
  meterAt = now;
  meterPct = pct;
}

static void renderMeter(uint16_t age) {
  // fill up over 500 ms, hold, fade out over the last 400 ms
  uint8_t level = (uint16_t)meterPct * N / 100;
  uint8_t fill = age < 500 ? (uint16_t)level * easeOut(age * 255 / 500) >> 8 : level;
  uint8_t fade = age > METER_MS - 400 ? (METER_MS - age) * 255 / 400 : 255;
  CRGB c = hsv(meterPct * 96 / 100, 255, 255);
  for (uint8_t i = 0; i < N; i++) {
    if (i < fill) {
      leds[i] = c;
      // tick marks every 10%
      if ((uint16_t)(i + 1) * 10 % N < 10) scaleC(leds[i], 60);
      scaleC(leds[i], fade);
    } else {
      leds[i] = CRGB(0, 0, 0);
    }
  }
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

  uint16_t meterAge = ageOf(now, meterAt);
  bool meter = meterAt && meterAge < METER_MS;
  if (phase == PH_OFF && !preview && !meter) {
    sys.ext = 0;
    if (dark) return false;
    for (uint8_t i = 0; i < N; i++) leds[i] = CRGB(0, 0, 0);
    dark = true;
    return true;
  }
  dark = false;
  if (meter && phase == PH_OFF) {
    renderMeter(meterAge);
    return true;
  }

  // --- coverage: which part of the blade is extended this frame
  uint16_t tipQ = (uint16_t)N << 8;  // extension in 1/256 LED
  bool fromTip = false, dissolve = false;
  uint8_t tipGlow = 0, flicker = 255;
  int16_t bolt = -1;
  if (phase == PH_IGNITE) {
    if (animStyle == IGN_PHOTON) {
      // a bolt races to the tip, then the blade fills back down from it
      if (p < 90) {
        bolt = (uint16_t)p * N / 90;
        tipQ = 0;
      } else {
        tipQ = (uint32_t)easeOut((uint16_t)(p - 90) * 255 / 165) * N;
        fromTip = true;
      }
      sys.ext = p;
    } else {
      uint8_t e = easeOut(p);
      if (animStyle == IGN_STUTTER) {
        // stalls and surges, with the whole blade sputtering
        int16_t j = (int16_t)(hash8((uint16_t)(age / 70)) & 63) - 32;
        e = clamp8((int16_t)e + j);
        if (hash8(frame, age / 30) < 90) flicker = 70;
      }
      tipQ = (uint32_t)e * N;
      tipGlow = animStyle == IGN_SPARK ? 255 : 150;
      sys.ext = e;
    }
  } else if (phase == PH_RETRACT) {
    if (animStyle == IGN_PHOTON) {
      dissolve = true;
      sys.ext = 255 - p;
    } else {
      uint8_t e = 255 - easeIn(p);
      if (animStyle == IGN_STUTTER && hash8(frame, age / 25) < 110) flicker = 60;
      tipQ = (uint32_t)e * N;
      tipGlow = animStyle == IGN_SPARK ? 220 : 90;
      sys.ext = e;
    }
  } else {
    sys.ext = 255;
  }
  uint8_t tipIdx = tipQ >> 8, tipFrac = tipQ & 0xFF;

  // --- per-frame colours and effect ages
  uint8_t pi = cfg.preset;
  uint8_t style = cfg.style[pi];
  uint8_t sat = presetSat(pi);
  CRGB c1 = hsv(sys.hue, sat, 255);
  CRGB c2 = hsv(sys.hue + (presetHue2(pi) - cfg.hue[pi]), sat, 255);
  uint8_t flick = hash8(frame) >> 4;

  uint16_t aClash = ageOf(now, sys.clashAt), aBlast = ageOf(now, sys.blastAt);
  uint16_t aStab = ageOf(now, sys.stabAt), aForce = ageOf(now, sys.forceAt), aHit = ageOf(now, hitAt);
  bool fxClash = sys.clashAt && aClash < 240;
  bool fxBlast = sys.blastAt && aBlast < 380;
  bool fxStab = sys.stabAt && aStab < 450;
  bool fxForce = sys.forceAt && aForce < 900;
  bool fxHit = hitAt && aHit < 520;
  uint8_t clashA = fxClash ? (uint16_t)(240 - aClash) * (240 - aClash) / 240 : 0;
  uint8_t blastR = 2 + aBlast / 22;
  uint8_t blastFade = fxBlast ? 255 - (uint32_t)aBlast * 255 / 380 : 0;
  uint16_t blastStep = (uint16_t)blastFade * 255 / (blastR + 1);
  uint8_t stabF = fxStab ? (uint32_t)(450 - aStab) * 255 / 450 : 0;
  uint8_t blastPos = (uint16_t)sys.blastPos * N >> 8;
  uint8_t forceFront = (uint32_t)aForce * (N + 24) / 620;
  uint8_t lock = sys.lockup;
  if (lock != lastLockup) {
    lastLockup = lock;
    lockPos = (uint8_t)(((uint16_t)N * (150 + (hash8((uint16_t)now) & 70))) >> 8);
  }
  uint8_t lockR = 8;
  uint8_t meltLen = 0;
  if (lock == LOCK_MELT) {
    uint16_t la = ageOf(now, sys.lockupAt) / 45;
    meltLen = la > 34 ? 34 : la;
  }
  uint16_t meltStep = meltLen ? 1020 / meltLen : 0;
  bool trainCue = sys.training && sys.trainPhase == TRAIN_INCOMING;
  uint8_t cuePos = (uint16_t)sys.trainPos * N >> 8;
  uint8_t tipZone = N - N / 3;

  for (uint8_t i = 0; i < N; i++) {
    // ---- coverage
    uint8_t cov;
    if (bolt >= 0) {
      uint8_t d = dist8(i, (uint8_t)bolt);
      if (d > 4) {
        leds[i] = CRGB(0, 0, 0);
        continue;
      }
      leds[i] = c1;
      blendC(leds[i], 255, 255, 255, 255 - d * 50);
      scaleC(leds[i], 255 - d * 40);
      continue;
    }
    if (dissolve) {
      int16_t v = ((int16_t)hash8((uint16_t)(i * 29 + 7)) + 40 - (int16_t)((uint16_t)p * 295 >> 8)) * 6;
      cov = clamp8(v);
    } else {
      uint8_t k = fromTip ? N - 1 - i : i;
      cov = k < tipIdx ? 255 : k == tipIdx ? tipFrac : 0;
    }
    if (!cov) {
      leds[i] = CRGB(0, 0, 0);
      continue;
    }

    // ---- base style
    CRGB c;
    switch (style) {
      default:
      case STYLE_STABLE: {
        c = c1;
        scaleC(c, 208 + (vnoise(i << 5, t << 1) >> 3) + flick);
        break;
      }
      case STYLE_UNSTABLE: {
        uint8_t n = vnoise((uint16_t)(i * 70), t * 6);
        c = c1;
        blendC(c, c2, n >> 2);
        uint8_t v = 105 + ((uint16_t)n * 150 >> 8);
        if (hash8((uint8_t)(i >> 3), frame) < 22) v >>= 1;  // dropouts
        scaleC(c, v);
        if (hash8(i, (uint16_t)(frame * 3 + 1)) > 247) blendC(c, 255, 255, 255, 190);  // crackle
        break;
      }
      case STYLE_PULSE: {
        uint8_t w = tsin8((uint8_t)((t >> 3) - i * 3));
        c = c1;
        blendC(c, c2, w >> 2);
        scaleC(c, 110 + ((uint16_t)w * 145 >> 8));
        break;
      }
      case STYLE_FIRE: {
        uint8_t h1 = vnoise((uint16_t)(i * 48 - (t >> 1) * 3), t * 3);
        uint8_t h2 = vnoise((uint16_t)(i * 110 + 7777), t * 5);
        uint8_t heat = ((uint16_t)h1 * 3 + h2) >> 2;
        heat = clamp8((int16_t)heat * 3 / 2 - 40);
        c = hsv(sys.hue + (heat >> 4), 255 - (heat > 200 ? (heat - 200) * 3 : 0), 40 + ((uint16_t)heat * 215 >> 8));
        break;
      }
      case STYLE_RAINBOW:
        c = hsv((uint8_t)(sys.hue + i * 2 - (t >> 3)), sat, 255);
        break;
      case STYLE_PLASMA: {
        uint8_t n = vnoise((uint16_t)(i * 64), t * 3);
        uint8_t n2 = vnoise((uint16_t)(i * 23 + 9000), t * 2);
        c = c1;
        blendC(c, c2, n);
        scaleC(c, 160 + ((uint16_t)n2 * 95 >> 8));
        break;
      }
      case STYLE_CANDY: {
        uint8_t w = tsin8((uint8_t)(i * 10 - (t >> 2)));
        c = c1;
        blendC(c, c2, clamp8(((int16_t)w - 128) * 4 + 128));
        break;
      }
      case STYLE_FLOW: {
        uint8_t w = tsin8((uint8_t)(i * 7 - (t >> 2)));
        uint8_t pk = w > 200 ? (w - 200) * 4 : 0;
        c = c1;
        scaleC(c, 150 + (w >> 2));
        blendC(c, c2, pk);
        blendC(c, 255, 255, 255, pk >> 2);
        break;
      }
    }

    // ---- motion: swinging heats the blade, most at the fast-moving tip
    if (sw) {
      uint8_t a = sw >> 3;
      if (i > tipZone) a += (uint16_t)sw * (i - tipZone) >> 7;
      blendC(c, 255, 255, 255, a);
    }

    // ---- ignition tip flare
    if (tipGlow && !fromTip) {
      uint8_t d = tipIdx > i ? tipIdx - i : 0;
      if (d < 5) blendC(c, 255, 255, 255, tipGlow - d * (tipGlow / 5));
      if (animStyle == IGN_SPARK && i < tipIdx && hash8(i, frame) > 244) blendC(c, 255, 240, 200, 230);
    }

    // ---- lockups
    if (lock == LOCK_CLASH) {
      scaleC(c, 200 + (hash8((uint16_t)(frame * 3)) & 55));
      uint8_t d = dist8(i, lockPos);
      if (d < lockR) blendC(c, 255, 255, 230, (uint16_t)(lockR - d) * (150 + (hash8(i, frame) & 105)) >> 3);
    } else if (lock == LOCK_DRAG) {
      if (i >= N - 14) {
        uint8_t h = hash8(i, frame);
        c = CRGB(255, 110 + (h >> 1), h >> 3);
        scaleC(c, 120 + (h & 135));
      }
    } else if (lock == LOCK_MELT) {
      if (meltLen && i >= N - meltLen) {
        uint8_t heat = (uint16_t)(i - (N - meltLen)) * meltStep >> 2;
        heat = (uint16_t)heat * (170 + (vnoise((uint16_t)(i * 90), t * 8) >> 2)) >> 8;
        blendC(c, 255, (uint16_t)heat * 170 >> 8, heat > 200 ? (heat - 200) * 3 : 0, 110 + (heat >> 1) + (heat >> 2));
      }
    } else if (lock == LOCK_LIGHTNING) {
      scaleC(c, 70);
      uint8_t n = vnoise((uint16_t)(i * 110), (uint16_t)(t * 30));
      if (n > 180) blendC(c, 200, 210, 255, clamp8((n - 180) * 4));
      if ((hash8((uint16_t)(frame * 5)) & 31) == 0) blendC(c, 255, 255, 255, 140);
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
    if (fxStab && i > N - 32) blendC(c, 255, 190, 90, (uint16_t)(i - (N - 32)) * stabF >> 5);
    if (fxForce) {
      uint8_t d = dist8(i, forceFront);
      if (d < 14) blendC(c, 255, 255, 255, (14 - d) * 13);
      else if (i < forceFront) scaleC(c, 190);
    }
    if (trainCue) {
      uint8_t d = dist8(i, cuePos);
      if (d < 6) blendC(c, 255, 30, 10, (frame & 4) ? 255 - d * 30 : 120 - d * 15);
    }
    if (fxHit) {
      scaleC(c, 50 + (hash8(frame) & 127));
      blendC(c, 255, 0, 0, 90);
    }

    if (flicker != 255) scaleC(c, flicker);
    if (cov != 255) scaleC(c, cov);
    leds[i] = c;
  }
  return true;
}
