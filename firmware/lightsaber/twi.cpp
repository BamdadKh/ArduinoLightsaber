#include "twi.h"
#include <avr/io.h>
#include <util/twi.h>

#define TWI_HZ 400000UL

void twiInit() {
  PORTC |= _BV(4) | _BV(5); // weak internal pull-ups on top of the GY-521's own
  TWSR = 0;
  TWBR = ((F_CPU / TWI_HZ) - 16) / 2;
  TWCR = _BV(TWEN);
}

// Wait for the current bus operation; ~5 ms worst case before giving up.
static bool twiWait() {
  uint16_t n = 0;
  while (!(TWCR & _BV(TWINT))) {
    if (++n == 0) return false;
  }
  return true;
}

static void twiStop() {
  TWCR = _BV(TWINT) | _BV(TWEN) | _BV(TWSTO);
  uint8_t n = 0;
  while ((TWCR & _BV(TWSTO)) && ++n) {}
}

// Recover from a stuck bus: reset the peripheral.
static bool twiFail() {
  TWCR = 0;
  TWCR = _BV(TWEN);
  return false;
}

static bool twiStart(uint8_t sla) {
  TWCR = _BV(TWINT) | _BV(TWSTA) | _BV(TWEN);
  if (!twiWait()) return false;
  uint8_t st = TW_STATUS;
  if (st != TW_START && st != TW_REP_START) return false;
  TWDR = sla;
  TWCR = _BV(TWINT) | _BV(TWEN);
  if (!twiWait()) return false;
  st = TW_STATUS;
  return st == TW_MT_SLA_ACK || st == TW_MR_SLA_ACK;
}

static bool twiSend(uint8_t b) {
  TWDR = b;
  TWCR = _BV(TWINT) | _BV(TWEN);
  return twiWait() && TW_STATUS == TW_MT_DATA_ACK;
}

bool twiWriteReg(uint8_t addr, uint8_t reg, uint8_t val) {
  if (!twiStart(addr << 1) || !twiSend(reg) || !twiSend(val)) return twiFail();
  twiStop();
  return true;
}

bool twiReadRegs(uint8_t addr, uint8_t reg, uint8_t* buf, uint8_t len) {
  if (!twiStart(addr << 1) || !twiSend(reg)) return twiFail();
  if (!twiStart((addr << 1) | 1)) return twiFail();
  while (len--) {
    // ACK every byte except the last
    TWCR = _BV(TWINT) | _BV(TWEN) | (len ? _BV(TWEA) : 0);
    if (!twiWait()) return twiFail();
    *buf++ = TWDR;
  }
  twiStop();
  return true;
}
