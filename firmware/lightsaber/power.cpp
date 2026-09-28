#include "power.h"
#include "config.h"
#include "state.h"
#include "platform.h"

// Resting Li-ion discharge curve, mV at 0, 10, ... 100 %
static const uint16_t CURVE[11] PROGMEM = {3300, 3480, 3590, 3660, 3710, 3760, 3820, 3900, 3980, 4070, 4150};

static uint32_t lastSample, lowSince;
static uint8_t goodCount;

static uint16_t readVccMv() {
  ADMUX = _BV(REFS0) | 0x0E; // AVcc reference, measure the 1.1 V bandgap
  delayMicroseconds(300);
  ADCSRA |= _BV(ADSC);
  while (ADCSRA & _BV(ADSC)) {}
  ADCSRA |= _BV(ADSC); // second conversion, first one after a mux switch is noisy
  while (ADCSRA & _BV(ADSC)) {}
  uint16_t raw = ADC;
  return raw ? (uint32_t)BANDGAP_MV * 1023 / raw : 5000;
}

static uint8_t percentFor(uint16_t mv) {
  if (mv <= pgm_read_word(&CURVE[0])) return 0;
  for (uint8_t i = 1; i < 11; i++) {
    uint16_t hi = pgm_read_word(&CURVE[i]);
    if (mv < hi) {
      uint16_t lo = pgm_read_word(&CURVE[i - 1]);
      return (i - 1) * 10 + (uint32_t)(mv - lo) * 10 / (hi - lo);
    }
  }
  return 100;
}

void powerInit() {
  sys.battPct = 100;
  sys.battMv = 0;
}

void powerUpdate(uint32_t now) {
#if BATTERY_SENSE
  if (now - lastSample < 400) return;
  lastSample = now;
  uint16_t vcc = readVccMv();
  analogRead(PIN_BATTERY); // switches the mux back; throw away
  uint16_t raw = analogRead(PIN_BATTERY);
  uint16_t mv = (uint32_t)raw * vcc / 1023;

  // A floating pin wanders; a real cell reads in range, sample after sample.
  if (mv > 2700 && mv < 4500) {
    if (goodCount < 3 && ++goodCount == 3) sys.battMv = mv;
  } else {
    goodCount = 0;
  }
  sys.battPresent = goodCount == 3;
  if (!sys.battPresent) {
    sys.battLow = false;
    return;
  }
  sys.battMv = (sys.battMv * 7u + mv) / 8;
  // under blade load the cell sags ~0.15 V; judge charge on a load-corrected value
  uint16_t est = sys.battMv + (sys.mode == MODE_ON ? 150 : 0);
  uint8_t pct = percentFor(est);
  // hysteresis so the readout doesn't flicker between two values
  if (pct + 1 < sys.battPct || pct > sys.battPct + 1 || pct == 0 || pct == 100) sys.battPct = pct;
  sys.battLow = sys.battMv < BATT_WARN_MV;
  if (sys.battMv < BATT_CUTOFF_MV) {
    if (!lowSince) lowSince = now;
  } else {
    lowSince = 0;
  }
#else
  (void)now;
#endif
}

bool powerCritical() { return lowSince && sys.now - lowSince > 5000; }
