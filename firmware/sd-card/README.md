# SD card

`MP3/` and `ADVERT/` go in the root of a FAT32 micro-SD card. The DFPlayer finds files in these
folders by number, so copy order doesn't matter. `build_sd_card.py` fills both (see the main
README); the files are committed so a clone is ready to copy.

```
MP3/      main channel: hum loop, ignite, retract, lockup loop, menu sounds
ADVERT/   short sounds played over the hum: swings, clashes, blaster blocks
```

The hum loops on the main channel. Every short effect is a DFPlayer advert, which pauses the
hum, plays, and lets it carry on. That is how one decoder layers sound without a BUSY pin.

The blade sounds are from the TeensySF font in the ProffieOS default SD card, by Fredrik
Hubinette, [CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/). The menu and boot
sounds are synthesized by `make_ui_sounds.py`.

## Tracks

`MP3/`

| File | Sound | Notes |
|------|-------|-------|
| 0001 | boot chime | plays once at power-up |
| 0002 | hum | looped |
| 0003 | ignite | plays once, then the hum starts |
| 0004 | retract | plays once |
| 0005 | lockup | looped while AUX is held |
| 0006 | menu tick | |
| 0007 | menu confirm | |
| 0008 | menu back | |
| 0009 | low battery | |

`ADVERT/`

| Files | Sound |
|-------|-------|
| 0001-0004 | slow swings, picked at random, never the same twice in a row |
| 0005-0008 | fast swings (peak above `SWING_FAST_DPS`) |
| 0009-0012 | clashes, played when a lockup ends |
| 0013-0016 | blaster blocks |
| 0017 | menu tick while the blade is lit |
| 0018 | colour change |

## Using your own sounds

- WAV or MP3, mono, 16-bit, 22.05 to 44.1 kHz. Keep the four-digit number and the extension. If
  a DFPlayer clone refuses WAV, convert with `ffmpeg -i 0002.wav -q:a 2 0002.mp3`.
- Loops (hum, lockup) should be cut at a zero crossing so they repeat without a click. MP3
  encoders add a few milliseconds of padding, so WAV loops are cleaner.
- Without a BUSY pin the firmware can't hear when a sound ends. It stops each one-shot by its
  length, which lives in `firmware/lightsaber/sound_lengths.h` (written by `build_sd_card.py`).
  Trim silence off the end of one-shots, or the hum will start late.
