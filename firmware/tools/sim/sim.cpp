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
  settingsDefaults();
  sys.imuOk = true;
  sys.audio = AUDIO_OK;
  sys.onBattery = true;
  sys.battMv = 3910;
  sys.battPct = 76;
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
  cfg.preset = 2;
  snap("idle");
  cfg.preset = 4;
  sys.battLow = true;
  sys.battPct = 9;
  g_now = 200300; snap("idle_lowbatt");

  resetSys();
  g_now = 300000;
  sys.mode = MODE_ON;
  sys.ext = 255;
  snap("on_still");
  motion.swing = 180;
  motion.swingDps = 830;
  snap("on_swing");
  motion.swing = 0;
  sys.lockup = true;
  snap("on_lockup");
  sys.lockup = false;
  sys.blastAt = g_now - 120;
  sys.blastPos = 150;
  snap("on_blaster");
  sys.blastAt = 0;
  sys.ext = 120;
  snap("on_igniting");
  sys.ext = 255;

  uiToast("LOW BATT", -1, 1000);
  snap("toast");
  sys.toast = nullptr;

  sys.mode = MODE_MENU;
  sys.ext = 0;
  for (int i = 0; i < MI_COUNT; i++) {
    sys.menuItem = (uint8_t)i;
    char n[32];
    snprintf(n, sizeof n, "menu_%02d", i);
    snap(n);
  }

  // ------------------------------------------------------------ blade timelines
  resetSys();
  sys.mode = MODE_ON;
  bool retracted = false;
  runBlade("ignite", 1000, 2800, [&](uint32_t t) {
    if (t == 1000) bladeIgnite(t);
    if (t >= 2000 && !retracted) {
      bladeRetract(t);
      retracted = true;
    }
  });

  for (int pr = 0; pr < NUM_PRESETS; pr++) {
    resetSys();
    cfg.preset = (uint8_t)pr;
    bladeIgnite(1000, true);
    std::string n = std::string("look_") + std::to_string(pr);
    runBlade(n.c_str(), 1200, 3200, [&](uint32_t t) {
      // one swing in the middle: intensity ramps up and decays
      int k = (int)t - 2000;
      motion.swing = (k > 0 && k < 500) ? (uint8_t)(k < 150 ? k * 255 / 150 : 255 - (k - 150) * 255 / 350) : 0;
    });
    bladeRetract(3300);
    runBlade("tmp", 3300, 4200, [](uint32_t) {});
  }

  resetSys();
  bladeIgnite(500, true);
  runBlade("effects", 800, 3800, [&](uint32_t t) {
    if (t == 1000) bladeClash(t);
    if (t == 1600) bladeBlast(t, 140);
    if (t == 2200) { sys.lockup = true; }
    if (t == 3200) sys.lockup = false;
  });

  puts("done");
  return 0;
}
