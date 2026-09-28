// Stand-ins for the AVR/Arduino bits used by the pure-logic modules, so the
// simulator can compile ui.cpp, blade.cpp, gfx.h etc. unchanged on a PC.
#pragma once
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>

#define PROGMEM
#define PSTR(s) (s)
#define pgm_read_byte(p) (*(const uint8_t*)(p))
#define pgm_read_word(p) (*(const uint16_t*)(p))
#define pgm_read_ptr(p) (*(const void* const*)(p))
#define memcpy_P memcpy
#define strncpy_P strncpy
#define strlen_P strlen
#define A6 20
#define A7 21

uint32_t millis();
