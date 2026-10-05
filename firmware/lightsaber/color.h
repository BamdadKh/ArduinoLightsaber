// Pixel type and colour helpers (replaces FastLED; ~3 KB of flash saved).
#pragma once
#include <stdint.h>

// Memory order is the WS2812B wire order (G, R, B), so a frame can be sent as-is.
struct CRGB {
  uint8_t g, r, b;
  CRGB() : g(0), r(0), b(0) {}
  CRGB(uint8_t r_, uint8_t g_, uint8_t b_) : g(g_), r(r_), b(b_) {}
};

// HSV to RGB, hue 0-255 round the colour wheel (0 red, 85 green, 170 blue).
CRGB hsv(uint8_t h, uint8_t s, uint8_t v);

// Send a frame. Scales in place by brightness (0-255) and to a current budget.
void ledShow(CRGB* px, uint8_t n, uint8_t brightness, uint16_t maxMa);
