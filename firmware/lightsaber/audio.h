// DFPlayer Mini driver + sound design layer.
//
// The DFPlayer has a single decoder, but its "advert" command pauses the current
// track, plays a clip from /ADVERT, then resumes. So the hum loops on the main
// channel (/MP3) and every short effect (swing, clash, blaster...) is an advert
// on top of it. No BUSY pin needed.
//
// Serial is TX-only bit-bang at 9600 baud on D11. SoftwareSerial RX would be
// corrupted anyway by the LED strip's interrupt-free 4 ms frames. RX is only
// polled once at boot to detect the module and the SD card.
#pragma once
#include <stdint.h>

// /MP3/000N.wav|mp3 - main channel
enum : uint8_t {
  SND_BOOT = 1, SND_HUM, SND_IGNITE, SND_RETRACT,
  SND_LOCKUP, SND_DRAG, SND_MELT, SND_LIGHTNING,
  SND_UI_TICK, SND_UI_OK, SND_UI_BACK, SND_LOWBATT, SND_SLEEP,
};
// /ADVERT/000N.wav|mp3 - overlays while something is playing
enum : uint8_t {
  ADV_SWING_SLOW = 1,  // 1-4
  ADV_SWING_FAST = 5,  // 5-8
  ADV_CLASH = 9,       // 9-12
  ADV_BLASTER = 13,    // 13-16
  ADV_STAB = 17, ADV_FORCE, ADV_TICK, ADV_TRAIN_SHOT, ADV_TRAIN_HIT,
  ADV_TRAIN_OVER, ADV_PRESET, ADV_OK,
};

enum AudioStatus : uint8_t { AUDIO_UNKNOWN, AUDIO_OK, AUDIO_NO_CARD, AUDIO_NO_MODULE };

void audioInit();
AudioStatus audioProbe();                 // blocking, ~150 ms
void audioUpdate(uint32_t now);

void audioSetVolume(uint8_t v);           // 0-30, persistent level
void audioMute(bool m);
void audioPlay(uint8_t track);            // one-shot on the main channel
void audioLoop(uint8_t track);            // looping on the main channel
void audioPlayThenLoop(uint8_t track, uint16_t ms, uint8_t loopTrack); // e.g. ignite -> hum
void audioAdvert(uint8_t track);          // effect overlay (latest request wins)
void audioAdvertRandom(uint8_t first, uint8_t count);
void audioStop();
void audioSwell(uint8_t intensity);       // 0-255 swing intensity -> hum volume swell
bool audioBusy();                         // commands still queued
