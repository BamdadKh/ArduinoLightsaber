#include "color.h"
#include "config.h"
#include "platform.h"

CRGB hsv(uint8_t h, uint8_t s, uint8_t v) {
  uint16_t h6 = (uint16_t)h * 6;
  uint8_t sector = h6 >> 8, f = h6 & 0xFF;
  uint8_t p = (uint16_t)v * (255 - s) >> 8;
  uint8_t q = (uint16_t)v * (255 - ((uint16_t)s * f >> 8)) >> 8;
  uint8_t t = (uint16_t)v * (255 - ((uint16_t)s * (255 - f) >> 8)) >> 8;
  switch (sector) {
    case 0: return CRGB(v, t, p);
    case 1: return CRGB(q, v, p);
    case 2: return CRGB(p, v, t);
    case 3: return CRGB(p, q, v);
    case 4: return CRGB(t, p, v);
    default: return CRGB(v, p, q);
  }
}

#ifdef ARDUINO
extern volatile unsigned long timer0_millis;

// WS2812B at 800 kHz on a 16 MHz AVR: 20 cycles per bit, high for 5 (0) or 13 (1).
// Bit loop after Adafruit_NeoPixel's proven 16 MHz timing.
static void sendBytes(const uint8_t* ptr, uint16_t count) {
  volatile uint8_t* port = &PORTD;
  const uint8_t mask = _BV(PIN_LED); // D6 = PD6
  uint8_t hi = PORTD | mask, lo = PORTD & ~mask;
  uint8_t b = *ptr++, next = lo, bit = 8;
  asm volatile(
    "head20:\n\t"
    "st   %a[port], %[hi]\n\t"
    "sbrc %[byte], 7\n\t"
    "mov  %[next], %[hi]\n\t"
    "dec  %[bit]\n\t"
    "st   %a[port], %[next]\n\t"
    "mov  %[next], %[lo]\n\t"
    "breq nextbyte20\n\t"
    "rol  %[byte]\n\t"
    "rjmp .+0\n\t"
    "nop\n\t"
    "st   %a[port], %[lo]\n\t"
    "nop\n\t"
    "rjmp .+0\n\t"
    "rjmp head20\n\t"
    "nextbyte20:\n\t"
    "ldi  %[bit], 8\n\t"
    "ld   %[byte], %a[ptr]+\n\t"
    "st   %a[port], %[lo]\n\t"
    "nop\n\t"
    "sbiw %[count], 1\n\t"
    "brne head20\n"
    : [port] "+e"(port), [byte] "+r"(b), [bit] "+r"(bit), [next] "+r"(next), [count] "+w"(count),
      [ptr] "+e"(ptr)
    : [hi] "r"(hi), [lo] "r"(lo));
}

void ledShow(CRGB* px, uint8_t n, uint8_t brightness, uint16_t maxMa) {
  uint8_t* p = (uint8_t*)px;
  uint16_t bytes = (uint16_t)n * 3;
  // Current estimate: ~20 mA per channel at full, ~1 mA idle per LED.
  uint32_t sum = 0;
  for (uint16_t i = 0; i < bytes; i++) sum += p[i];
  uint32_t ma = (sum * brightness / 255) * 20 / 255;
  uint8_t scale = brightness;
  uint16_t budget = maxMa > n ? maxMa - n : 1;
  if (ma > budget) scale = (uint32_t)brightness * budget / ma;
  if (scale != 255)
    for (uint16_t i = 0; i < bytes; i++) p[i] = ((uint16_t)p[i] * (scale + 1)) >> 8;

  static uint16_t lostUs;
  DDRD |= _BV(PIN_LED);
  uint8_t sreg = SREG;
  cli();
  sendBytes(p, bytes);
  // Timer0 overflows were blocked for bytes*10 us; one of them is still pending
  // and will count itself. Credit the rest so millis() keeps real time.
  lostUs += bytes * 10;
  lostUs = lostUs > 1024 ? lostUs - 1024 : 0;
  timer0_millis += lostUs / 1000;
  lostUs %= 1000;
  SREG = sreg;
  delayMicroseconds(60); // latch
}
#endif
