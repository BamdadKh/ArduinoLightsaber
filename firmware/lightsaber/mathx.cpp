#include "mathx.h"

// quarter sine wave, 0..127
static const uint8_t SIN_Q[17] PROGMEM = {0, 12, 25, 36, 48, 59, 69, 79, 90, 98, 106, 112, 117, 121, 125, 127, 127};

uint16_t isqrt32(uint32_t n) {
  uint32_t r = 0, b = 1UL << 30;
  while (b > n) b >>= 2;
  while (b) {
    if (n >= r + b) {
      n -= r + b;
      r = (r >> 1) + b;
    } else {
      r >>= 1;
    }
    b >>= 2;
  }
  return (uint16_t)r;
}

uint8_t hash8(uint16_t x) {
  x ^= x >> 7;
  x *= 0x2F6B;
  x ^= x >> 9;
  x *= 0x1D35;
  return (uint8_t)(x ^ (x >> 8));
}

static uint8_t smooth8(uint8_t f) { return (uint8_t)(((uint32_t)f * f * (768u - 2u * f)) >> 16); }

uint8_t vnoise(uint16_t x, uint16_t t) {
  uint8_t xi = x >> 8, ti = t >> 8;
  uint8_t fx = smooth8(x & 0xFF), ft = smooth8(t & 0xFF);
  uint8_t a = hash8(xi, ti), b = hash8((uint8_t)(xi + 1), ti);
  uint8_t c = hash8(xi, (uint16_t)(ti + 1)), d = hash8((uint8_t)(xi + 1), (uint16_t)(ti + 1));
  return lerp8(lerp8(a, b, fx), lerp8(c, d, fx), ft);
}

uint8_t tsin8(uint8_t p) {
  uint8_t i = p & 63;
  if (p & 64) i = 64 - i;
  uint8_t idx = i >> 2, fr = i & 3;
  uint8_t a = pgm_read_byte(&SIN_Q[idx]);
  uint8_t v = idx < 16 ? a + (((pgm_read_byte(&SIN_Q[idx + 1]) - a) * fr) >> 2) : a;
  return (p & 128) ? 128 - v : 128 + v;
}

static uint16_t rngState = 0xACE1;
uint8_t rand8() {
  rngState ^= rngState << 7;
  rngState ^= rngState >> 9;
  rngState ^= rngState << 8;
  return (uint8_t)rngState;
}
