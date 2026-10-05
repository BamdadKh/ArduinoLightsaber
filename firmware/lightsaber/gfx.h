// Scanline graphics for the 32x128 portrait OLED. Every primitive answers one
// question: "which pixels of row y do I cover?" as a 32-bit mask (bit 31 = x 0).
// Screens OR these together per row, so nothing is ever stored.
#pragma once
#include "platform.h"
#include "assets.h"

typedef uint32_t Row;

#define SCR_W 32
#define SCR_H 128

static inline Row pix(int16_t x) { return (x < 0 || x > 31) ? 0 : 0x80000000UL >> x; }

static inline Row span(int16_t x0, int16_t x1) {
  if (x0 < 0) x0 = 0;
  if (x1 > 31) x1 = 31;
  if (x0 > x1) return 0;
  return (0xFFFFFFFFUL >> x0) & (0xFFFFFFFFUL << (31 - x1));
}

// Place a w-bit field (MSB = leftmost pixel) with its left edge at x.
static inline Row place(uint32_t bits, uint8_t w, int16_t x) {
  if (x >= 32 || x + w <= 0) return 0;
  if (x < 0) {
    w += x;
    bits &= (1UL << w) - 1;
    x = 0;
  }
  int8_t sh = 32 - x - w;
  return sh >= 0 ? bits << sh : bits >> -sh;
}

// Double every bit: 0b101 becomes 0b110011 (for 2x scaled glyphs and icons).
static inline uint16_t dbl8(uint8_t b) {
  uint16_t r = 0;
  for (uint8_t i = 0; i < 8; i++)
    if (b & (1 << i)) r |= 3u << (2 * i);
  return r;
}

// Ordered 4x4 Bayer dither: level 0 (black) .. 16 (white) for row y.
static inline Row dither(uint8_t level, uint8_t y) {
  static const uint8_t BAYER[16] PROGMEM = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
  if (level >= 16) return 0xFFFFFFFFUL;
  uint8_t n = 0;
  for (uint8_t j = 0; j < 4; j++)
    if (pgm_read_byte(&BAYER[(y & 3) * 4 + j]) < level) n |= 8 >> j;
  return n * 0x11111111UL;
}

static inline uint8_t textWidth(uint8_t len, uint8_t scale) { return len ? (len * 6 - 1) * scale : 0; }

static inline uint8_t strLen(const char* s, bool pgm) {
  uint8_t n = 0;
  while (pgm ? pgm_read_byte(s + n) : s[n]) n++;
  return n;
}

// One row of a string. pgm = the string lives in flash.
static Row textRow(const char* s, bool pgm, int16_t x, int16_t y0, int16_t y, uint8_t scale = 1) {
  int16_t r = y - y0;
  if (r < 0 || r >= FONT_H * scale) return 0;
  r /= scale;
  Row out = 0;
  for (;;) {
    char c = pgm ? (char)pgm_read_byte(s) : *s;
    if (!c || x >= 32) break;
    s++;
    if (x > -6 * scale && c > FONT_FIRST && c <= FONT_LAST) {
      uint8_t g = pgm_read_byte(&FONT5X7[(c - FONT_FIRST) * FONT_H + r]);
      out |= scale == 1 ? place(g, 5, x) : place(dbl8(g), 10, x);
    }
    x += 6 * scale;
  }
  return out;
}

// Centred text, or a scrolling marquee if it doesn't fit.
static Row textFit(const char* s, bool pgm, int16_t y0, int16_t y, uint16_t t, uint8_t scale = 1) {
  int16_t r = y - y0;
  if (r < 0 || r >= FONT_H * scale) return 0;
  uint8_t w = textWidth(strLen(s, pgm), scale);
  if (w <= 32) return textRow(s, pgm, (32 - w) / 2, y0, y, scale);
  uint16_t loop = w + 12;
  int16_t off = (int16_t)((t / 45) % loop);
  return textRow(s, pgm, -off, y0, y, scale) | textRow(s, pgm, loop - off, y0, y, scale);
}

// 16x16 icon at x, 1x or 2x.
static Row iconRow(uint8_t id, int16_t x, int16_t y0, int16_t y, uint8_t scale) {
  int16_t r = y - y0;
  if (r < 0 || r >= 16 * scale) return 0;
  r /= scale;
  const uint8_t* p = &ICONS16[(id * 16 + r) * 2];
  uint8_t hi = pgm_read_byte(p), lo = pgm_read_byte(p + 1);
  if (scale == 1) return place((uint16_t)hi << 8 | lo, 16, x);
  return place((uint32_t)dbl8(hi) << 16 | dbl8(lo), 32, x);
}

// Generic 1x bitmap, bytesPerRow 1 or 2, MSB = left.
static Row bmpRow(const uint8_t* bmp, uint8_t bpr, uint8_t w, uint8_t h, int16_t x, int16_t y0, int16_t y) {
  int16_t r = y - y0;
  if (r < 0 || r >= h) return 0;
  const uint8_t* p = bmp + r * bpr;
  uint16_t v = bpr == 2 ? (uint16_t)pgm_read_byte(p) << 8 | pgm_read_byte(p + 1) : pgm_read_byte(p);
  return place(v >> (bpr * 8 - w), w, x);
}

// Decimal into buf (no padding); returns length.
static uint8_t fmtNum(char* buf, uint16_t v) {
  char tmp[6];
  uint8_t n = 0;
  do {
    tmp[n++] = '0' + v % 10;
    v /= 10;
  } while (v);
  for (uint8_t i = 0; i < n; i++) buf[i] = tmp[n - 1 - i];
  buf[n] = 0;
  return n;
}
