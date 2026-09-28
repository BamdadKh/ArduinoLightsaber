#include "audio.h"
#include "config.h"
#include "settings.h"
#include "mathx.h"

#define TX_BIT _BV(3) // D11 = PB3 -> DFPlayer RX
#define RX_BIT _BV(2) // D10 = PB2 <- DFPlayer TX
#define BIT_CYCLES (F_CPU / 9600)

enum : uint8_t {
  CMD_VOLUME = 0x06, CMD_PLAY_MP3 = 0x12, CMD_ADVERT = 0x13, CMD_STOP_ADVERT = 0x15,
  CMD_STOP = 0x16, CMD_SINGLE_LOOP = 0x19, CMD_QUERY_FILES = 0x48,
};

struct Cmd {
  uint8_t cmd, arg;
};
static Cmd queue[8];
static uint8_t qHead, qLen;
static uint8_t pendingAdvert;
static uint8_t loopNext;      // track to loop once the one-shot ends
static uint32_t loopAt;
static uint32_t lastSend, lastAdvert, lastVolume;
static uint8_t baseVol = 20, sentVol = 255, swell;
static bool muted, looping;

// ---------------------------------------------------------------- wire level

// One 8N1 byte. Interrupts are held off only for the data bits (~0.95 ms, less
// than one millis() tick, so no time is lost).
static void txByte(uint8_t b) {
  uint8_t sreg = SREG;
  cli();
  PORTB &= ~TX_BIT;
  __builtin_avr_delay_cycles(BIT_CYCLES - 6);
  for (uint8_t i = 0; i < 8; i++) {
    if (b & 1) PORTB |= TX_BIT;
    else PORTB &= ~TX_BIT;
    b >>= 1;
    __builtin_avr_delay_cycles(BIT_CYCLES - 10);
  }
  PORTB |= TX_BIT;
  SREG = sreg;
  delayMicroseconds(110); // stop bit
}

static void sendFrame(uint8_t cmd, uint16_t arg) {
  uint8_t f[10] = {0x7E, 0xFF, 0x06, cmd, 0x00, (uint8_t)(arg >> 8), (uint8_t)arg, 0, 0, 0xEF};
  uint16_t sum = 0;
  for (uint8_t i = 1; i < 7; i++) sum += f[i];
  sum = -sum;
  f[7] = sum >> 8;
  f[8] = sum;
  for (uint8_t i = 0; i < 10; i++) txByte(f[i]);
}

static int16_t rxByte(uint16_t timeoutMs) {
  uint32_t t0 = millis();
  while (PINB & RX_BIT) {
    if (millis() - t0 > timeoutMs) return -1;
  }
  uint8_t sreg = SREG;
  cli();
  __builtin_avr_delay_cycles(BIT_CYCLES + BIT_CYCLES / 2 - 30); // middle of bit 0
  uint8_t b = 0;
  for (uint8_t i = 0; i < 8; i++) {
    b >>= 1;
    if (PINB & RX_BIT) b |= 0x80;
    __builtin_avr_delay_cycles(BIT_CYCLES - 9);
  }
  SREG = sreg;
  return b;
}

void audioInit() {
  PORTB |= TX_BIT; // idle high
  DDRB |= TX_BIT;
  DDRB &= ~RX_BIT;
  PORTB |= RX_BIT;
}

AudioStatus audioProbe() {
  sendFrame(CMD_QUERY_FILES, 0);
  uint8_t f[10];
  uint8_t n = 0;
  bool heard = false;
  uint32_t t0 = millis();
  while (millis() - t0 < 160) {
    int16_t c = rxByte(40);
    if (c < 0) continue;
    heard = true;
    if (n == 0 && c != 0x7E) continue;
    f[n++] = (uint8_t)c;
    if (n < 10) continue;
    n = 0;
    if (f[3] == CMD_QUERY_FILES) return (f[5] | f[6]) ? AUDIO_OK : AUDIO_NO_CARD;
    if (f[3] == 0x40 && f[6] != 1) return AUDIO_NO_CARD; // error other than "busy"
  }
  return heard ? AUDIO_UNKNOWN : AUDIO_NO_MODULE;
}

// ---------------------------------------------------------------- queue

static void push(uint8_t cmd, uint8_t arg) {
  if (qLen < 8) {
    queue[(qHead + qLen) & 7] = {cmd, arg};
    qLen++;
  }
}

static void resetMain() {
  qLen = 0;
  loopNext = 0;
  pendingAdvert = 0;
  if (millis() - lastAdvert < 3000) push(CMD_STOP_ADVERT, 0); // don't let a resume fight us
}

void audioPlay(uint8_t track) {
  resetMain();
  if (looping) push(CMD_SINGLE_LOOP, 1);
  looping = false;
  push(CMD_PLAY_MP3, track);
}

void audioLoop(uint8_t track) {
  resetMain();
  push(CMD_PLAY_MP3, track);
  push(CMD_SINGLE_LOOP, 0);
  looping = true;
}

void audioPlayThenLoop(uint8_t track, uint16_t ms, uint8_t loopTrack) {
  audioPlay(track);
  loopNext = loopTrack;
  loopAt = millis() + ms;
}

void audioAdvert(uint8_t track) { pendingAdvert = track; }

void audioAdvertRandom(uint8_t first, uint8_t count) {
  static uint8_t last;
  uint8_t pick = first + rand8() % count;
  if (pick == last && count > 1) pick = first + (pick - first + 1) % count; // never the same twice
  last = pick;
  audioAdvert(pick);
}

void audioStop() {
  resetMain();
  if (looping) push(CMD_SINGLE_LOOP, 1);
  push(CMD_STOP, 0);
  looping = false;
}

void audioSetVolume(uint8_t v) { baseVol = v > 30 ? 30 : v; }
void audioMute(bool m) { muted = m; }
void audioSwell(uint8_t intensity) { swell = (uint16_t)intensity * cfg.boost * 3 / 255; }
bool audioBusy() { return qLen || pendingAdvert || loopNext; }

void audioUpdate(uint32_t now) {
  if (loopNext && (int32_t)(now - loopAt) >= 0) {
    push(CMD_PLAY_MP3, loopNext);
    push(CMD_SINGLE_LOOP, 0);
    looping = true;
    loopNext = 0;
  }
  if (now - lastSend < AUDIO_CMD_GAP_MS) return;

  if (qLen) {
    Cmd c = queue[qHead];
    qHead = (qHead + 1) & 7;
    qLen--;
    sendFrame(c.cmd, c.arg);
    lastSend = now;
    return;
  }
  if (pendingAdvert) {
    sendFrame(CMD_ADVERT, pendingAdvert);
    pendingAdvert = 0;
    lastSend = lastAdvert = now;
    return;
  }
  // Volume last: swell changes are frequent but least important. Small steps are
  // rate-limited so the serial line stays free for effects.
  uint8_t want = muted ? 0 : baseVol + swell;
  if (want > 30) want = 30;
  if (want != sentVol) {
    uint8_t diff = want > sentVol ? want - sentVol : sentVol - want;
    if (sentVol == 255 || (now - lastVolume >= 90 && (diff >= 2 || now - lastVolume > 250))) {
      sendFrame(CMD_VOLUME, want);
      sentVol = want;
      lastSend = lastVolume = now;
    }
  }
}
