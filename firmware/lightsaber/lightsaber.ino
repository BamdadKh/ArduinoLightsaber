/*
  KYBER OS 2 - Arduino Nano lightsaber firmware

  Hardware (V3 PCB): Nano, MPU-6050 (A4/A5), DFPlayer Mini (D10/D11), 144 x WS2812B (D6),
  main button D2, aux button D3, 128x32 SSD1306 on D8/D9 mounted portrait. The cell feeds
  the 5V pin directly; its voltage is read as Vcc.

  Module map
    saber.cpp    modes and controls -> effects (the brain)
    imu.cpp      MPU-6050: swing and clash
    blade.cpp    LED blade look, ignition, effect overlays
    ui.cpp       portrait OLED screens, streamed as scanlines (no framebuffer)
    audio.cpp    DFPlayer: looping hum + advert overlays, TX-only bit-bang serial
    buttons.cpp  two buttons: clicks, holds, chords
    power.cpp    battery voltage (Vcc against the internal bandgap)
    settings.cpp EEPROM settings and the colour table

  Every loop() pass does a slice of everything; nothing blocks for more than one
  LED frame (~4.5 ms) or one DFPlayer command (~10 ms).
*/
#include "config.h"
#include "state.h"
#include "settings.h"
#include "twi.h"
#include "imu.h"
#include "audio.h"
#include "blade.h"
#include "buttons.h"
#include "power.h"
#include "ui.h"
#include "saber.h"

#if DEBUG_SERIAL
// Telemetry to the USB serial port (115200 8N1) without HardwareSerial's ~1.7 KB:
// a TX-only bit-bang on D1, one line every 250 ms:
// L loops/s, M mode, S swing dps, B battery mV (rested cell; see power.cpp)
#define DBG_BIT (F_CPU / 115200)
static void dbgByte(uint8_t b) {
  uint8_t sreg = SREG;
  cli();
  PORTD &= ~_BV(1);
  __builtin_avr_delay_cycles(DBG_BIT - 4);
  for (uint8_t i = 0; i < 8; i++) {
    if (b & 1) PORTD |= _BV(1);
    else PORTD &= ~_BV(1);
    b >>= 1;
    __builtin_avr_delay_cycles(DBG_BIT - 9);
  }
  PORTD |= _BV(1);
  __builtin_avr_delay_cycles(DBG_BIT);
  SREG = sreg;
}
static void dbgStr(const char* p) {
  while (uint8_t c = pgm_read_byte(p++)) dbgByte(c);
}
static void dbgNum(const char* label, int16_t v) {
  dbgStr(label);
  if (v < 0) {
    dbgByte('-');
    v = -v;
  }
  char b[6];
  uint8_t n = 0;
  do b[n++] = '0' + v % 10; while (v /= 10);
  while (n) dbgByte(b[--n]);
  dbgByte(' ');
}
static void debugPrint(uint32_t now) {
  static uint32_t last;
  static uint16_t loops;
  loops++;
  if (now - last < 250) return;
  last = now;
  dbgNum(PSTR("L="), loops * 4);
  dbgNum(PSTR("M="), sys.mode);
  dbgNum(PSTR("S="), motion.swingDps);
  dbgNum(PSTR("B="), sys.battMv);
  dbgByte(13); // CR LF
  dbgByte(10);
  loops = 0;
}
#endif

void setup() {
#if DEBUG_SERIAL
  PORTD |= _BV(1);
  DDRD |= _BV(1);
#endif
  settingsLoad();
  buttonsInit();
  audioInit();
  bladeInit();
  bladeSetBrightness(cfg.bright);
  uiInit();
  twiInit();
  sys.imuOk = imuInit();
  if (sys.imuOk) imuCalibrateBias(false); // only takes if the saber is lying still
  powerInit();
  saberInit(millis());
}

void loop() {
  uint32_t now = millis();
  sys.now = now;
  buttonsUpdate(now);

  if (sys.imuOk) imuUpdate(now);

  saberUpdate(now);
  audioUpdate(now);
  powerUpdate(now);
  if (bladeRender(now)) ledShow(leds, NUM_LEDS, bladeBrightness(), LED_MAX_MA);
  uiPump(now);
  settingsUpdate(now);

#if DEBUG_SERIAL
  debugPrint(now);
#endif
}
