# Hardware

> **Source of truth:** the fabricated V3 PCB (`hardware/fabrication/lightsaber-v3-gerbers.zip`)
> and the firmware. The KiCad schematic has known mistakes that were worked around
> in code after the boards were ordered — do not trust it over the board.

## Bill of materials

| Ref | Part | Notes |
|-----|------|-------|
| A1 | Arduino Nano v3 (ATmega328P) | Socketed on 2×15 headers |
| U1 | GY-521 (MPU-6050) | I²C, address `0x68`; gyro + accel (±2000 °/s, ±16 g) |
| U2 | DFRobot DFPlayer Mini (DFR0299) | micro-SD with sound files, see [`firmware/sd-card`](../firmware/sd-card/README.md) |
| J1 | WS2812B strip, 144 LEDs | 3-pin header: GND / data / 5V |
| J2 | Momentary push button (main) | To GND, internal pull-up |
| J4 | Momentary push button (aux) | To GND, internal pull-up |
| J3 | **SSD1306 OLED** (as built) | The schematic calls this header Bluetooth (D8/D9), but the OLED is wired here and runs on software I²C. SDA → D8 (white), SCL → D9 (yellow) |
| J5 | Intended for the OLED (I²C on A4/A5) | Unused as built |
| J7 | Speaker/amp header | DFPlayer `DAC_R` + 5V + GND → external amp |
| J6 | 5V / GND power in | |
| J8 | Battery sense | **Not routed on the PCB** — see known issues |
| — | 4× M2 mounting holes | 2.2 mm |

## Pin map (as built)

| Nano pin | Net | Connected to | Used in firmware |
|----------|-----|--------------|------------------|
| D2  | `d2`  | J2 main button | ✅ `PIN_BTN_MAIN` |
| D3  | `d3`  | J4 aux button | ✅ `PIN_BTN_AUX` |
| D6  | `d6`  | J1 WS2812B data | ✅ `PIN_LED` |
| D8  | `d8`  | J3 → OLED SDA (white) | ✅ software I²C (`oled.cpp`, PB0) |
| D9  | `d9`  | J3 → OLED SCL (yellow) | ✅ software I²C (`oled.cpp`, PB1) |
| D10 | —     | DFPlayer TX | ✅ polled once at boot to detect the player (`audio.cpp`, PB2) |
| D11 | —     | DFPlayer RX | ✅ bit-banged 9600 baud TX (`audio.cpp`, PB3) |
| A4  | `sda` | MPU-6050 (+ J5, unused) | ✅ I²C |
| A5  | `scl` | MPU-6050 (+ J5, unused) | ✅ I²C |
| A7  | `a7`  | J8 battery (bodge wire, see below) | ✅ `PIN_BATTERY` |

## Known issues

Found by comparing the firmware, schematic and PCB, and by running DRC.

What checks out:
- Every routed pin the firmware uses (D2, D3, D6, D10, D11, A4, A5) matches both the schematic and the PCB.
- The Nano, DFPlayer and GY-521 footprints have pads in the same physical order as the real modules.
- The 5V and GND nets are copper pours, not thin traces.

Board issues:
- **A7 battery sense is not routed.** `J8` and `A1` pin 26 are only a ratsnest line in the PCB,
  with no copper, so battery monitoring needs a bodge wire from J8 to A7. It connects straight
  to the cell's + terminal with no divider, which is fine because the cell never exceeds Vcc.
  The firmware measures Vcc against the internal 1.1 V bandgap, so readings stay right when
  the boost output moves. It only trusts A7 after several in-range samples, so a board without
  the bodge just hides the battery gauge (`BATTERY_SENSE` in `config.h` turns it off
  completely).
- **DFPlayer BUSY (pin 16) is not connected.** The firmware is designed around this: the hum
  is looped by the player itself, effects are "adverts" that resume it, and one-shot lengths
  come from `sound_lengths.h`.
- **The DFPlayer's built-in amp is unused.** SPK1/SPK2 aren't connected. Audio leaves through
  `DAC_R` (right channel only) on J7, so an external amplifier is required.
- **No 1 kΩ series resistor** between Nano TX and DFPlayer RX. DFRobot recommends one to reduce noise and hiss.
- **Wire holes are too small.** J1, J2, J4, J6, J7 and J8 use 1.00 mm pitch header footprints with
  **0.5 mm drills**, and J3 and J5 use 1.27 mm pitch with 0.65 mm drills. Only thin solid-core
  wire fits (roughly 24 AWG or smaller). That's a problem for J1 and J6, which carry the LED
  strip's power, since the strip can pull several amps.
- **The OLED is on D8/D9, not the hardware I²C bus.** It's wired to J3, the header the schematic
  labels for Bluetooth, so it has to run on software I²C and can't share the MPU-6050's A4/A5 bus.
  Wire it as OLED SDA → D8, SCL → D9. Go by the pin labels on the OLED, not the wire colours.
- **The power system isn't in the schematic or PCB.** The board only has a `5V/G` input (J6) and a
  `bat+` pad (J8). The battery, charging, switch and 5V regulation all live off the board and
  aren't documented yet (TODO).
- **No LED current limit in hardware.** 144 WS2812B LEDs at full white draw about 8.6 A.
  The firmware estimates each frame's current and scales it to `LED_MAX_MA` (2.2 A default,
  set it to what your boost converter can deliver). Even when dark, the strip idles at roughly
  1 mA per LED, so sleep mode doesn't stop that drain. Use the hilt's power switch for storage.
- DRC on the PCB file reports 47 violations: 25 isolated copper, 7 clearance, 7 courtyard overlap, and minor silk issues.
- `fp-lib-table` points to a `DFR0299.pretty` in the author's Downloads folder, which no longer exists.
  The board still opens because its footprints are stored inside the `.kicad_pcb`.

## Hilt / 3D model

| File | What it is |
|------|------------|
| `hardware/3d/lightsaber-hilt.f3z` | Fusion 360 archive of the hilt assembly (editable source, Git LFS) |
| `hardware/3d/hilt-tube.stl` | Printable hilt tube mesh; GitHub renders it in a 3D viewer |
| `hardware/3d/lightsaber-pcb.step` | STEP of the assembled PCB, exported from KiCad |

The hilt is a tube with a cut-away channel. The PCB sits in the upper section and the
off-board modules stack below it. Components placed in the assembly:

| Component | Role |
|-----------|------|
| Lightsaber PCB (V3) | Main board, see above |
| 18650 battery holder | Single-cell Li-ion supply |
| TP4056 charging module | USB-C charging for the 18650 |
| DC-DC XL6009 | Boost from cell voltage to 5 V |
| PAM8403 3 W amplifier | External amp fed from J7 (`DAC_R`) |
| Flat speaker (35×25) | |
| 2× tactile push button (B3F) | Main (J2) and aux (J4) buttons |
| OLED 0.91" SSD1306 128×32 | Status display on J3, mounted along the hilt |
| HC-06 Bluetooth module | Placeholder; Bluetooth is shelved and J3 is used by the OLED |

To update the model, export a fresh `.f3z` from Fusion (**File → Export → Fusion Archive**) over
the existing file. Use `.f3z` rather than `.f3d` so the linked component models come along.
Re-export the tube with right-click on the body → **Save As Mesh → STL** to `hilt-tube.stl`.

## Regenerating exports

KiCad 8 CLI (`C:\Program Files\KiCad\8.0\bin\kicad-cli.exe`):

```bash
kicad-cli sch export pdf -o hardware/exports/lightsaber-schematic.pdf hardware/kicad/lightsaber.kicad_sch
kicad-cli sch export svg -o hardware/exports hardware/kicad/lightsaber.kicad_sch
kicad-cli pcb export svg --layers F.Cu,F.Silkscreen,Edge.Cuts --page-size-mode 2 --exclude-drawing-sheet -o hardware/exports/lightsaber-pcb-front.svg hardware/kicad/lightsaber.kicad_pcb
kicad-cli pcb export svg --layers B.Cu,B.Silkscreen,Edge.Cuts --mirror --page-size-mode 2 --exclude-drawing-sheet -o hardware/exports/lightsaber-pcb-back.svg hardware/kicad/lightsaber.kicad_pcb
```

Do **not** regenerate `lightsaber-v3-gerbers.zip` — it is the exact file sent to the fab.
