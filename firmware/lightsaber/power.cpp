#include "power.h"
#include "config.h"
#include "state.h"
#include "platform.h"

// The cell is wired straight to the Nano's 5V pin, so Vcc is the cell voltage. Two numbers
// matter and they must never be mixed up:
//   rest   the cell with the blade dark (sampled once the load has been gone for a moment).
//          This drives the percentage, the low warning and the lockout.
//   loaded the cell with the blade lit. It sags 0.2-0.5 V under a full strip, which is normal,
//          so it is only used as a last-resort brown-out guard.
// Warnings latch: one warning per low spell, never one per ignition.

// Resting Li-ion discharge curve, mV at 0, 10, ... 100 %
static const uint16_t CURVE[11] PROGMEM = {3300, 3480, 3590, 3660, 3710, 3760, 3820, 3900, 3980, 4070, 4150};

#define SAMPLE_MS 250
#define SETTLE_MS 1500       // after the blade goes dark, wait this long before trusting the cell
#define SAG_HOLD_MS 1500     // loaded voltage must stay below BATT_SAG_MV this long
#define RECHARGED_MV 3900    // a brown-out lockout only clears once the cell is charged again

static uint32_t lastSample, darkAt, sagSince;
static uint16_t rest, loaded;
static bool wasOn, haveRest, sagLocked;

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
  sys.onBattery = true;
}

void powerUpdate(uint32_t now) {
  if (now - lastSample < SAMPLE_MS) return;
  lastSample = now;
  uint16_t mv = readVccMv();

  if (mv > BATT_USB_MV) { // USB power: the number says nothing about the cell
    sys.onBattery = false;
    sagSince = 0;
    return;
  }
  sys.onBattery = true;

  bool on = sys.mode == MODE_ON;
  if (wasOn && !on) darkAt = now;
  wasOn = on;

  if (on) {
    loaded = (loaded * 3u + mv) / 4;
    if (loaded >= BATT_SAG_MV) sagSince = 0;
    else if (!sagSince) sagSince = now;
    if (sagSince && now - sagSince > SAG_HOLD_MS) {
      sagLocked = true;
      sys.battEmpty = true;
    }
    return;
  }
  sagSince = 0;
  if (haveRest && now - darkAt < SETTLE_MS) return;

  rest = haveRest ? (rest * 7u + mv) / 8 : mv;
  uint8_t pct = percentFor(rest);
  // hysteresis so the readout doesn't flicker between two values
  if (!haveRest || pct + 1 < sys.battPct || pct > sys.battPct + 1 || pct == 0 || pct == 100) sys.battPct = pct;
  haveRest = true;
  sys.battMv = rest;

  if (!sys.battLow && rest < BATT_LOW_MV) sys.battLow = true;
  else if (sys.battLow && rest > BATT_LOW_MV + 100) sys.battLow = false;

  if (rest < BATT_EMPTY_MV) sys.battEmpty = true;
  else if (sys.battEmpty && rest >= (sagLocked ? RECHARGED_MV : BATT_EMPTY_MV + 150)) {
    sys.battEmpty = sagLocked = false;
  }
}
