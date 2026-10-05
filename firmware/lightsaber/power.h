// Battery monitor. The cell feeds the Nano's Vcc directly, so Vcc is measured against the
// internal 1.1 V bandgap; no sense wire needed. See power.cpp for how rest and loaded
// voltage are told apart.
#pragma once
#include <stdint.h>

void powerInit();
void powerUpdate(uint32_t now);   // updates sys.battMv / battPct / battLow / battEmpty / onBattery
