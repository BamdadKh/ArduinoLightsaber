// The saber's brain: turns button and motion events into modes, effects and sounds.
#pragma once
#include <stdint.h>

void saberInit(uint32_t now);
void saberUpdate(uint32_t now);
