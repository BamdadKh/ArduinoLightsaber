// Blade renderer: the blade look, ignition/retraction animations and effect overlays,
// all computed per pixel per frame without extra buffers (only the LED array).
#pragma once
#include <stdint.h>
#include "config.h"
#include "color.h"

extern CRGB leds[NUM_LEDS];

void bladeInit();
void bladeIgnite(uint32_t now, bool quick = false);
void bladeRetract(uint32_t now);
bool bladeSettled();    // fully on or fully off, no ignition/retraction running
void bladeClash(uint32_t now);
void bladeBlast(uint32_t now, uint8_t pos);
void bladeSetBrightness(uint8_t level);      // 1-10
uint8_t bladeBrightness();                   // 0-255 for ledShow()
// Render one frame into leds[]. Returns false if nothing changed (strip already dark).
bool bladeRender(uint32_t now);
