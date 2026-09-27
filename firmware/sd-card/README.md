# SD card layout

The DFPlayer Mini plays tracks by index. Format the micro-SD as FAT32 and copy the files
**in this order** (the DFPlayer uses FAT write order for `play(n)`, not the filename):

| Track | File | Used for |
|-------|------|----------|
| 1 | `0001.mp3` | Ignite |
| 2 | `0002.mp3` | Hum (should be a seamless loop) |
| 3 | `0003.mp3` | Swing |
| 4 | `0004.mp3` | Retract |

The sound files themselves aren't in the repo yet. Put them in this folder
(`firmware/sd-card/`) so the card can be rebuilt from the repo.
