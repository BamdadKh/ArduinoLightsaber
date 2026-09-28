// Minimal blocking I2C master on the ATmega328P TWI peripheral (A4/A5), 400 kHz.
// Replaces Wire: no 5x32 B buffers, and every wait has a timeout so a glitched
// bus can't hang the saber.
#pragma once
#include <stdint.h>

void twiInit();
bool twiWriteReg(uint8_t addr, uint8_t reg, uint8_t val);
bool twiReadRegs(uint8_t addr, uint8_t reg, uint8_t* buf, uint8_t len);
