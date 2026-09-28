// PC simulator: runs the real UI and blade renderers and dumps what the OLED and
// the LED strip would show. Build + render: python firmware/tools/sim/run_sim.py
#include <stdio.h>
#include <string>
#include <vector>
#include <functional>
#include "../../lightsaber/state.h"
#include "../../lightsaber/settings.h"
#include "../../lightsaber/imu.h"
#include "../../lightsaber/ui.h"
#include "../../lightsaber/blade.h"
#include "../../lightsaber/audio.h"

Sys sys;
Motion motion;
static uint32_t g_now;
uint32_t millis() { return g_now; }

static std::string outDir = ".";

// ---- OLED: 32x128 portrait, written as PGM
static void snap(const char* name) {
  sys.now = g_now;
  uiBeginFrame(g_now);
  std::string path = outDir + "/ui_" + name + ".pgm";
  FILE* f = fopen(path.c_str(), "wb");
  fprintf(f, "P5\n32 128\n255\n");
  for (int y = 0; y < 128; y++) {
    uint32_t r = uiRow((uint8_t)y);
    for (int x = 0; x < 32; x++) fputc((r & (0x80000000UL >> x)) ? 255 : 0, f);
  }
  fclose(f);
}

// ---- Blade: time runs left to right, LEDs bottom (hilt) to top (tip), written as PPM

static void runBlade(const char* name, uint32_t t0, uint32_t t1, std::function<void(uint32_t)> script) {
  std::vector<CRGB> img;
  int frames = 0;
  for (uint32_t t = t0; t < t1; t += 8) {
    g_now = t;
    sys.now = t;
    script(t);
    bladeRender(t);
    for (int i = 0; i < NUM_LEDS; i++) img.push_back(leds[i]);
    frames++;
  }
  std::string path = outDir + "/blade_" + name + ".ppm";
  FILE* f = fopen(path.c_str(), "wb");
  fprintf(f, "P6\n%d %d\n255\n", frames, NUM_LEDS);
  for (int y = NUM_LEDS - 1; y >= 0; y--)
    for (int x = 0; x < frames; x++) {
      CRGB c = img[x * NUM_LEDS + y];
      fputc(c.r, f);
      fputc(c.g, f);
      fputc(c.b, f);
    }
  fclose(f);
}

static void resetSys() {
  memset(&sys, 0, sizeof(sys));
  memset(&motion, 0, sizeof(motion));
  settingsDefaults(false);
  sys.imuOk = true;
  sys.audio = AUDIO_OK;
  sys.battPresent = true;
  sys.battMv = 3910;
  sys.battPct = 76;
  sys.hue = cfg.hue[cfg.preset];
  motion.tempC = 29;
}

static void setPreset(uint8_t p) {
  cfg.preset = p;
  sys.hue = cfg.hue[p];
}

int main(int argc, char** argv) {
  if (argc > 1) outDir = argv[1];

  // ------------------------------------------------------------ OLED screens
  resetSys();
  sys.mode = MODE_BOOT;
  sys.bootAt = 100000;
  sys.audio = AUDIO_UNKNOWN;
  g_now = 100000 + 180; snap("boot_0180");
  g_now = 100000 + 1000; snap("boot_1000");
  sys.audio = AUDIO_OK;
  g_now = 100000 + 1500; snap("boot_1500");
  g_now = 100000 + 1850; snap("boot_1850");

  resetSys();
  g_now = 200000;
  sys.mode = MODE_OFF;
  setPreset(2);
  snap("idle");
  setPreset(5);
  sys.battLow = true;
  sys.battPct = 9;
  g_now = 200300; snap("idle_lowbatt");

  resetSys();
  g_now = 300000;
  sys.mode = MODE_ON;
  sys.ext = 255;
  motion.pitch = 35;
  snap("on_still");
  motion.swing = 180;
  motion.swingDps = 830;
  motion.pitch = -20;
  sys.combo = 4;
  sys.comboAt = g_now - 200;
  snap("on_swing");
  motion.swing = 0;
  sys.combo = 0;
  sys.lockup = LOCK_CLASH;
  snap("on_lockup");
  sys.lockup = LOCK_LIGHTNING;
  snap("on_lightning");
  sys.lockup = LOCK_NONE;
  sys.blastAt = g_now - 120;
  sys.blastPos = 150;
  snap("on_blaster");
  sys.blastAt = 0;
  sys.ext = 120;
  snap("on_igniting");
  sys.ext = 255;
  setPreset(2); // unstable style
  cfg.style[2] = STYLE_UNSTABLE;
  snap("on_unstable");

  // telemetry HUD
  sys.hud = true;
  for (int i = 0; i < HIST_LEN; i++) {
    int v = (int)(120 + 110 * ((i * 37 % 17) / 17.0) * ((i % 9) < 5 ? 1 : 0.2));
    sys.hist[i] = (uint8_t)v;
  }
  sys.histHead = 10;
  motion.swingDps = 734;
  sys.sessPeak = 1288;
  sys.combo = 3;
  sys.comboAt = g_now - 300;
  snap("hud");
  sys.hud = false;
  sys.combo = 0;

  // colour wheel
  sys.colorWheel = true;
  sys.hue = 40;
  snap("color_40");
  sys.hue = 170;
  snap("color_170");
  sys.colorWheel = false;

  // toast
  uiToast("SAVED", -1, 1000);
  snap("toast");
  sys.toast = nullptr;
  uiToast("LOW BATT", -1, 1000);
  snap("toast_marquee");
  sys.toast = nullptr;

  // training
  sys.training = true;
  sys.trainLives = 3;
  sys.trainScore = 0;
  sys.trainPhase = TRAIN_WAIT;
  g_now = 300256; // blink phase on
  snap("train_ready");
  sys.trainPhase = TRAIN_INCOMING;
  sys.trainScore = 7;
  sys.trainLives = 2;
  sys.trainWindow = 700;
  sys.trainAt = g_now - 350;
  sys.trainPos = 180;
  sys.trainReact = 312;
  snap("train_incoming");
  sys.trainPhase = TRAIN_RESULT;
  sys.trainHit = false;
  sys.trainAt = g_now - 150;
  snap("train_deflect");
  sys.trainHit = true;
  snap("train_hit");
  sys.trainPhase = TRAIN_OVER;
  cfg.trainBest = 12;
  snap("train_over");
  sys.training = false;

  // menu pages
  sys.mode = MODE_MENU;
  sys.ext = 0;
  cfg.ignitions = 142;
  cfg.clashes = 1093;
  cfg.peakDps = 1640;
  cfg.onSeconds = 3600 * 7 + 100;
  for (int i = 0; i < MI_COUNT; i++) {
    sys.menuItem = (uint8_t)i;
    char n[32];
    snprintf(n, sizeof n, "menu_%02d", i);
    snap(n);
  }

  // ------------------------------------------------------------ blade timelines
  static const char* IGN[] = {"scroll", "spark", "stutter", "photon"};
  for (int ig = 0; ig < 4; ig++) {
    resetSys();
    setPreset(0);
    cfg.ignition[0] = (uint8_t)ig;
    sys.mode = MODE_ON;
    std::string n = std::string("ignite_") + IGN[ig];
    bool retracted = false;
    runBlade(n.c_str(), 1000, 2800, [&](uint32_t t) {
      if (t == 1000) bladeIgnite(t);
      if (t >= 2000 && !retracted) {
        bladeRetract(t);
        retracted = true;
      }
    });
  }

  static const char* STY[] = {"stable", "unstable", "pulse", "fire", "rainbow", "plasma", "candy", "flow"};
  static const uint8_t PRESET_FOR_STYLE[] = {0, 2, 3, 4, 5, 6, 7, 1};
  for (int st = 0; st < 8; st++) {
    resetSys();
    setPreset(PRESET_FOR_STYLE[st]);
    cfg.style[cfg.preset] = (uint8_t)st;
    bladeIgnite(1000, true);
    std::string n = std::string("style_") + STY[st];
    runBlade(n.c_str(), 1200, 3200, [&](uint32_t t) {
      // one swing in the middle: intensity ramps up and decays
      int k = (int)t - 2000;
      motion.swing = (k > 0 && k < 500) ? (uint8_t)(k < 150 ? k * 255 / 150 : 255 - (k - 150) * 255 / 350) : 0;
    });
    bladeRetract(3300);
    runBlade("tmp", 3300, 4200, [](uint32_t) {});
  }

  resetSys();
  setPreset(0);
  bladeIgnite(500, true);
  runBlade("effects", 800, 5600, [&](uint32_t t) {
    if (t == 1000) bladeClash(t);
    if (t == 1400) bladeBlast(t, 140);
    if (t == 1800) bladeStab(t);
    if (t == 2200) bladeForce(t);
    if (t == 3000) { sys.lockup = LOCK_CLASH; sys.lockupAt = t; }
    if (t == 3600) { sys.lockup = LOCK_DRAG; sys.lockupAt = t; }
    if (t == 4200) { sys.lockup = LOCK_MELT; sys.lockupAt = t; }
    if (t == 4800) { sys.lockup = LOCK_LIGHTNING; sys.lockupAt = t; }
    if (t == 5400) sys.lockup = LOCK_NONE;
  });

  resetSys();
  runBlade("meter", 1000, 3800, [&](uint32_t t) {
    if (t == 1000) bladeMeter(t, 76);
  });

  resetSys();
  setPreset(0);
  bladeIgnite(500, true);
  sys.training = true;
  runBlade("train", 800, 2600, [&](uint32_t t) {
    if (t == 1000) { sys.trainPhase = TRAIN_INCOMING; sys.trainPos = 180; }
    if (t == 1600) { sys.trainPhase = TRAIN_RESULT; bladeBlast(t, 180); }
    if (t == 2000) bladeHitFlash(t);
  });

  printf("done\n");
  return 0;
}
