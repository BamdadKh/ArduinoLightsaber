// Battery monitor on A7. The ADC reference (Vcc from the boost converter) is measured
// against the internal 1.1 V bandgap each time, so readings stay right when Vcc sags.
#pragma once
#include <stdint.h>

void powerInit();
void powerUpdate(uint32_t now);   // updates sys.battMv / battPct / battLow / battPresent
bool powerCritical();             // below cutoff long enough to force the blade off
