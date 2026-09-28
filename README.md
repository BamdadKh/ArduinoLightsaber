# Arduino Lightsaber ⚔️

A motion-reactive lightsaber built around an Arduino Nano on a custom PCB. It has a
144-LED WS2812B blade, sound from a DFPlayer Mini, swing detection with an MPU-6050,
and one-button control.

<!-- Demo video goes here: drag an .mp4 into a GitHub issue to get a user-attachments link, or embed YouTube -->

## Features

Firmware **KYBER OS 2**, written from scratch for the V3 board:

- **8 blade styles:** solid, feral (unstable), pulse, fire, prism (rainbow), haze
  (plasma), candy and flow. Each one reacts to motion: swinging speeds up its animation
  and heats the fast-moving tip toward white.
- **4 ignition / retraction animations:** sweep, spark, surge (sputtering) and bolt
  (a bolt races to the tip, then the blade fills back down, and it dissolves on retract).
- **Motion engine on the MPU-6050 gyro and accelerometer:** swing accents (slow and
  fast banks), clash detection, stab, twist gestures, and blade pitch. Sensitivities
  are adjustable, and there's a one-press axis calibration.
- **Effects:** blaster block, lockup, drag (automatic when the tip points down), melt
  (stab during a lockup), lightning block and force push. Each has its own light show
  and sound.
- **Layered sound on a single DFPlayer:** a seamless hum loop with swing and clash
  "adverts" on top, plus a hum swell that follows swing speed. It comes with an
  original synthesized font of 37 tracks.
- **Colour wheel:** twist the hilt like a dial to tune the hue live.
- **Training droid game:** a remote fires at random points on the blade, and you swing
  or block before the window closes. The window shrinks as you score. 3 lives, best
  score saved.
- **Portrait OLED UI:** a live mini-saber mirroring the blade, a telemetry HUD with a
  swing waveform, and a 16-page icon menu. All of it is rendered as scanlines with
  no framebuffer.
- **Gesture ignition** (twist, swing or stab), muted ignition, and presets that remember
  your colour, style and ignition choices.
- **Battery monitoring:** measured against the internal bandgap, with a blade meter,
  a low-battery warning, and cutoff protection.
- **Idle auto-retract and sleep.** Settings and lifetime stats (ignitions, clashes,
  peak deg/s, hours) live in EEPROM.

<img src="docs/images/oled-screens.png" alt="OLED screens: boot self-test, ready, lit with swing meter, lockup, telemetry, colour wheel, training, menu" width="100%">

*The OLED screens as rendered by the firmware's own UI code in the PC simulator (1 px = 1 OLED pixel).*

## Controls

Two buttons: **MAIN** (D2) and **AUX** (D3).

| Blade off | |
|---|---|
| MAIN click | Ignite |
| MAIN double-click | Ignite muted |
| MAIN triple-click / both buttons | Battery check (meter on the blade) |
| MAIN hold 1.5 s | Sleep |
| AUX click / double-click | Next / previous preset |
| AUX hold | Menu |
| Twist the hilt (gesture on) | Ignite |

| Blade on | |
|---|---|
| MAIN click | Retract (or leave training) |
| MAIN double-click | Colour wheel: twist to tune, MAIN saves, AUX cancels |
| MAIN triple-click | Toggle blade view / telemetry HUD |
| MAIN hold | Lightning block (while held) |
| AUX click | Blaster block (instant) |
| AUX hold | Lockup while held. Tip down = drag, stab = melt |
| Both buttons | Force push |
| Swing, clash, stab | Detected automatically |
| Twist the hilt (gesture on) | Retract |

| Menu | |
|---|---|
| AUX click / double-click | Next / previous page |
| MAIN click | Change value / run action |
| MAIN hold 1.5 s | Factory reset (on the RESET page) |
| AUX hold | Save and exit |

Menu pages: sound, light, style, colour, ignition, swing and clash sensitivity, gesture
set, hum boost, sleep timer, screen flip, training, calibrate, stats, reset, exit.
Style, colour and ignition preview on the blade as you change them.

## Repository layout

```
firmware/
  lightsaber/                 Arduino sketch (Nano, ATmega328P), one module per subsystem
  sd-card/                    Sound font for the DFPlayer (MP3/ + ADVERT/ folders)
  tools/                      Asset and sound generators, PC simulator
hardware/
  kicad/                      KiCad 8 project (schematic + PCB)
  fabrication/                Gerber zip exactly as ordered (V3)
  exports/                    Schematic PDF/SVG, PCB front/back SVG
  3d/                         Fusion 360 hilt assembly (.f3z), printable tube (.stl), PCB STEP
docs/
  hardware.md                 BOM, as-built pin map, known board issues
  images/                     Diagrams and README images
media/
  videos/  photos/            Build and demo media (videos via Git LFS)
```

## Hardware

| | |
|---|---|
| MCU | Arduino Nano v3 |
| Motion | GY-521 / MPU-6050 (I²C) |
| Sound | DFPlayer Mini → external amp + speaker |
| Blade | WS2812B, 144 LEDs, data on D6 |
| Input | Main button D2, aux button D3 |
| Battery | 18650 sensed on A7 (J8) |
| Display | 0.91" SSD1306 128×32 OLED (mounted along the hilt), software I²C on D8 (SDA) / D9 (SCL) |

The full BOM, pin map and known PCB issues are in **[docs/hardware.md](docs/hardware.md)**.
The schematic is in [hardware/exports/lightsaber-schematic.pdf](hardware/exports/lightsaber-schematic.pdf).

| PCB front | PCB back |
|---|---|
| ![front](hardware/exports/lightsaber-pcb-front.svg) | ![back](hardware/exports/lightsaber-pcb-back.svg) |

### Hilt

The hilt is modelled in Fusion 360 as a full assembly with the PCB and every module in place:
[hardware/3d/lightsaber-hilt.f3z](hardware/3d/lightsaber-hilt.f3z). Open it in Fusion with
**File → Open → Open from my computer**. The linked component models are bundled in the archive.

<img src="docs/images/hilt-assembly.png" alt="Hilt assembly in Fusion 360" width="320">

**[View the hilt tube in 3D](hardware/3d/hilt-tube.stl)** (GitHub shows STL files in an interactive viewer).
The same file is ready to slice for printing.

Besides the PCB, the model holds the 18650 battery holder, TP4056 USB-C charger, XL6009 boost
converter, PAM8403 amp, flat speaker, two tactile buttons and the OLED. See
[docs/hardware.md](docs/hardware.md#hilt--3d-model) for details.

> The schematic has known mistakes. The V3 board was built from it and the firmware works
> around them, so treat the board and firmware as the real reference.

## Firmware

### Build & flash

No third-party libraries needed: the drivers for the LEDs, OLED, DFPlayer, I2C and
buttons are all built in.

```bash
arduino-cli compile -b arduino:avr:nano firmware/lightsaber
arduino-cli upload -b arduino:avr:nano -p COM3 firmware/lightsaber
```

Clone Nanos may need `arduino:avr:nano:cpu=atmega328old`. It builds to ~30.3 KB of the
30 KB flash and uses 54% of RAM. Build-time options (pins, LED count, current budget,
battery thresholds, motion thresholds) are in
[`firmware/lightsaber/config.h`](firmware/lightsaber/config.h).

For serial telemetry (swing, roll, pitch, shock, battery at 115200 baud on the USB port), build with:

```bash
arduino-cli compile -b arduino:avr:nano --build-property "compiler.cpp.extra_flags=-DDEBUG_SERIAL=1" firmware/lightsaber
```

**First run:**
1. If the screen is upside down, set it with menu > FLIP.
2. Stand the saber blade-up and run menu > CALIBRATE. This teaches it which sensor
   axis runs along the blade, which swing, stab, twist and pitch all depend on.

### Architecture

| Module | Job |
|--------|-----|
| `saber.cpp` | Modes, controls, gestures -> effects and sounds |
| `imu.cpp` | MPU-6050 at 1 kHz, +-2000 deg/s, +-16 g. Swing peak detection, high-pass clash, stab, twist, pitch, gyro drift tracking |
| `blade.cpp` | Per-pixel style + ignition + effect overlay compositor. The only buffer is the LED array |
| `color.cpp` | WS2812B driver (cycle-exact 16 MHz loop), current limiting, HSV |
| `ui.cpp`, `gfx.h`, `oled.cpp` | Portrait OLED UI, streamed as scanlines |
| `audio.cpp` | DFPlayer protocol, TX-only bit-banged serial, command queue, hum loop + adverts |
| `buttons.cpp` | Debounce, multi-click, hold, chords |
| `power.cpp` | Battery voltage against the 1.1 V bandgap |
| `settings.cpp` | EEPROM settings, presets, lifetime stats |

How it fits in 2 KB of RAM and a 16 MHz CPU:

- **No OLED framebuffer.** The SSD1306 runs in vertical addressing mode, so with the
  panel stood on end, each physical column is exactly one 32-pixel row of the portrait
  screen. Every screen is a function that returns one row as a `uint32_t`. The UI
  streams 16 rows per `loop()` pass, a full frame about every 8 passes, and the rows
  never exist in RAM together.
- **Nothing blocks.** The longest stall is one LED frame (4.3 ms) or one DFPlayer
  command (10 ms). The main loop runs at ~110 Hz, sampling motion, rendering the blade
  and streaming the display in turn.
- **Sound without BUSY.** Serial RX can't survive the LED strip's interrupt-free frames,
  so the player is driven blind. The hum is looped on the main channel, effects are
  adverts that resume it, and one-shot lengths come from `sound_lengths.h`. At boot,
  before the LEDs run, RX is polled once to detect the module and SD card.

### Tools

```bash
python firmware/tools/gen_assets.py     # font + pixel-art icons -> assets.h (edit the art in the script)
python firmware/tools/make_sounds.py    # synthesize the sound font -> sd-card/, sound_lengths.h
python firmware/tools/sim/run_sim.py    # compile ui/blade on the PC, render screens + blade timelines
```

The simulator (MSYS2 g++ or MSVC) runs the real `ui.cpp` and `blade.cpp` and writes
PNGs to `firmware/tools/out/`. Use it to design screens and effects without flashing.

<img src="docs/images/blade-effects.png" alt="Blade timelines from the simulator" width="520">

*Blade timelines from the simulator: time runs left to right, hilt at the bottom.
The bend in each style is a swing speeding the animation up.*

### SD card

Copy `firmware/sd-card/MP3` and `firmware/sd-card/ADVERT` to a FAT32 micro-SD. See
[firmware/sd-card/README.md](firmware/sd-card/README.md) for the track map and how to
use your own font.

## Status

The V3 PCB has been fabricated and the firmware runs on it. See the
[known issues](docs/hardware.md#known-issues) for hardware bugs.

## License

[MIT](LICENSE)
