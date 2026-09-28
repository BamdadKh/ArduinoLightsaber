# SD card (sound font)

Copy the two folders here, `MP3/` and `ADVERT/`, to the root of a FAT32 micro-SD card.
The DFPlayer finds files in these folders **by their number**, so copy order doesn't matter.

```
SD root
├── MP3/       main channel: hum loop, ignite, retract, lockup loops, UI sounds
└── ADVERT/    overlays: swings, clashes, blasters... played on top of the hum
```

The firmware loops the hum on the main channel. Every short effect is a DFPlayer
*advert*, which pauses the hum, plays, then lets the hum carry on. That's how one
decoder does layered sound without the BUSY pin.

The bundled font is synthesized from scratch by `firmware/tools/make_sounds.py`. It's
original and free to use. Regenerate it with:

```bash
python firmware/tools/make_sounds.py
```

## Track map

### `MP3/` (main channel)

| File | Sound | Notes |
|------|-------|-------|
| `0001` | Boot chime | Plays once the self-test finds the player |
| `0002` | **Hum** | Looped. Make it long (8 s here) so the loop restarts rarely |
| `0003` | Ignite | One-shot, then the hum starts. Length in `sound_lengths.h` |
| `0004` | Retract | One-shot. Length in `sound_lengths.h` |
| `0005` | Lockup | Looped while AUX is held |
| `0006` | Drag | Looped (lockup with the tip pointing down) |
| `0007` | Melt | Looped (stab during a lockup) |
| `0008` | Lightning block | Looped while MAIN is held |
| `0009` | UI tick | Menu navigation (blade off) |
| `0010` | UI confirm | |
| `0011` | UI back | |
| `0012` | Low battery | |
| `0013` | Power down | |

### `ADVERT/` (effects over the hum)

| Files | Sound |
|-------|-------|
| `0001`–`0004` | Slow swings (picked at random, never the same twice) |
| `0005`–`0008` | Fast swings (peak above `SWING_FAST_DPS`) |
| `0009`–`0012` | Clashes |
| `0013`–`0016` | Blaster deflects |
| `0017` | Stab |
| `0018` | Force push |
| `0019` | Tick (colour wheel, menu while lit) |
| `0020` | Training droid fires |
| `0021` | Training: you got hit |
| `0022` | Training: game over |
| `0023` | Preset change |
| `0024` | Confirm |

## Using your own font

- WAV and MP3 both work: mono, 16-bit, 22.05–44.1 kHz. Keep the 4-digit number and
  the extension, e.g. `0002.mp3`. If a DFPlayer clone refuses WAV, convert to MP3 with:
  `ffmpeg -i 0002.wav -q:a 2 0002.mp3`
- Loops (hum, lockup, drag, melt, lightning) should be cut on a zero crossing so they
  repeat without a click. MP3 encoders add a few ms of padding, so WAV loops are cleaner.
- The firmware can't hear when a one-shot ends, because BUSY isn't wired and serial RX
  is off while the blade runs. Put your ignite and retract lengths in
  `firmware/lightsaber/sound_lengths.h`, otherwise the hum starts early or late.
