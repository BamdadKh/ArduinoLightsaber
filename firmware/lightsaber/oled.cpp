#include "oled.h"
#include <avr/io.h>
#include <avr/pgmspace.h>

// Open-drain: a line is either driven low or released to its pull-up (never driven high,
// the panel's I/O runs at 3.3 V on most modules).
#define SDA_BIT _BV(0) // D8 = PB0
#define SCL_BIT _BV(1) // D9 = PB1
#define OLED_ADDR (0x3C << 1)

static inline void sdaLow() { PORTB &= ~SDA_BIT; DDRB |= SDA_BIT; }
static inline void sdaHigh() { DDRB &= ~SDA_BIT; PORTB |= SDA_BIT; }
static inline void sclLow() { PORTB &= ~SCL_BIT; DDRB |= SCL_BIT; }
static inline void sclHigh() { DDRB &= ~SCL_BIT; PORTB |= SCL_BIT; }

// ~500 kHz: fast enough to stream 16 rows in ~1 ms, slow enough for weak pull-ups.
static void wr(uint8_t b) {
  for (uint8_t m = 0x80; m; m >>= 1) {
    if (b & m) sdaHigh();
    else sdaLow();
    __builtin_avr_delay_cycles(5);
    sclHigh();
    __builtin_avr_delay_cycles(12);
    sclLow();
  }
  sdaHigh(); // ACK slot, not checked
  __builtin_avr_delay_cycles(5);
  sclHigh();
  __builtin_avr_delay_cycles(12);
  sclLow();
}

static void start(uint8_t control) {
  sdaLow();
  __builtin_avr_delay_cycles(8);
  sclLow();
  wr(OLED_ADDR);
  wr(control);
}

static void stop() {
  sdaLow();
  __builtin_avr_delay_cycles(8);
  sclHigh();
  __builtin_avr_delay_cycles(8);
  sdaHigh();
  __builtin_avr_delay_cycles(8);
}

static void cmds(const uint8_t* c, uint8_t n) {
  start(0x00);
  while (n--) wr(*c++);
  stop();
}

static void cmdsP(const uint8_t* c, uint8_t n) {
  start(0x00);
  while (n--) wr(pgm_read_byte(c++));
  stop();
}

static void cmd1(uint8_t a) { cmds(&a, 1); }

void oledInit(bool flip) {
  sdaHigh();
  sclHigh();
  static const uint8_t init[] PROGMEM = {
    0xAE,       // display off
    0xD5, 0x80, // clock
    0xA8, 0x1F, // multiplex 32
    0xD3, 0x00, // offset
    0x40,       // start line 0
    0x8D, 0x14, // charge pump on
    0x20, 0x01, // vertical addressing: the heart of the portrait streaming trick
    0xDA, 0x02, // COM pins for 128x32
    0x81, 0xCF, // contrast
    0xD9, 0xF1, // precharge
    0xDB, 0x40, // VCOM detect
    0x2E,       // no scrolling
    0xA4, 0xA6, // show RAM, normal polarity
  };
  cmdsP(init, sizeof(init));
  oledFlip(flip);
  oledWindowAll();
  oledRowsBegin();
  for (uint8_t y = 0; y < 128; y++) oledRow(0);
  oledRowsEnd();
  cmd1(0xAF);
}

void oledFlip(bool flip) {
  uint8_t c[2] = {(uint8_t)(flip ? 0xA0 : 0xA1), (uint8_t)(flip ? 0xC0 : 0xC8)};
  cmds(c, 2);
}

void oledInvert(bool on) { cmd1(on ? 0xA7 : 0xA6); }

void oledContrast(uint8_t c) {
  uint8_t b[2] = {0x81, c};
  cmds(b, 2);
}

void oledWindowAll() {
  static const uint8_t w[] PROGMEM = {0x21, 0, 127, 0x22, 0, 3};
  cmdsP(w, sizeof(w));
}

void oledRowsBegin() { start(0x40); }

void oledRow(uint32_t bits) {
  // page 0 holds physical rows 0-7 = portrait pixels 31..24, LSB first
  wr((uint8_t)bits);
  wr((uint8_t)(bits >> 8));
  wr((uint8_t)(bits >> 16));
  wr((uint8_t)(bits >> 24));
}

void oledRowsEnd() { stop(); }
