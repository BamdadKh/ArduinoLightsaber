// Small integer helpers shared by the motion, blade and UI code.
#pragma once
#include <stdint.h>
#include "platform.h"

uint16_t isqrt32(uint32_t n);

// Cheap integer hash, good enough for per-pixel/per-frame randomness without state.
uint8_t hash8(uint16_t x);
static inline uint8_t hash8(uint8_t a, uint16_t b) { return hash8((uint16_t)(b * 131u + a * 7919u)); }

static inline uint8_t clamp8(int16_t v) { return v < 0 ? 0 : v > 255 ? 255 : (uint8_t)v; }

// Linear 0..255 blend of a toward b
static inline uint8_t lerp8(uint8_t a, uint8_t b, uint8_t t) {
  return (uint8_t)(a + (((int16_t)b - a) * t >> 8));
}

// Smooth value noise over x with time t (both 8.8 fixed point lattice), 0..255.
uint8_t vnoise(uint16_t x, uint16_t t);

// Sine: phase 0..255 in, 0..255 out (phase 0 -> 128, rising)
uint8_t tsin8(uint8_t p);

// Small fast PRNG (xorshift)
uint8_t rand8();
