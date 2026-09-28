// Thin portability layer so the pure-logic modules (gfx, ui, blade) also build on a PC
// for the simulator in firmware/tools/sim. On the Nano it just pulls in the AVR headers.
#pragma once
#include <stdint.h>

#ifdef ARDUINO
#include <Arduino.h>
#include <avr/pgmspace.h>
#else
#include "host_shim.h"
#endif

#define NOINLINE __attribute__((noinline))
