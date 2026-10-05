#!/usr/bin/env python3
"""Build the SD card image in firmware/sd-card and the matching sound_lengths.h.

    python firmware/tools/build_sd_card.py

The blade sounds (hum, ignite, retract, lockup, swings, clashes, blaster blocks) come from the
TeensySF font in the ProffieOS default SD card by Fredrik Hubinette, CC BY-SA 4.0. The script
downloads that zip once (about 200 MB, cached in firmware/tools/out). Menu and boot sounds are
synthesized by make_ui_sounds.py. Needs numpy.
"""
import io
import shutil
import urllib.request
import wave
import zipfile
from pathlib import Path

import numpy as np

import make_ui_sounds

HERE = Path(__file__).resolve().parent
FW = HERE.parent
SD = FW / "sd-card"
URL = "https://fredrik.hubbe.net/lightsaber/ProffieOS_SD_Card.zip"
CACHE = HERE / "out" / "ProffieOS_SD_Card.zip"
FONT = "ProffieOS_SD_Card/TeensySF/"

# our track number: file in the font
MP3 = {2: "hum01", 3: "out02", 4: "in01", 5: "lock01"}
ADVERT = {
    1: "swng09", 2: "swng12", 3: "swng06", 4: "swng02",      # slow swings
    5: "swng01", 6: "swng08", 7: "swng16", 8: "swng03",      # fast swings
    9: "clsh03", 10: "clsh10", 11: "clsh01", 12: "clsh06",   # clashes
    13: "blst01", 14: "blst02", 15: "blst03", 16: "blst04",  # blaster blocks
}
# One-shots get their silent tail cut: the firmware stops them by length, and during an advert the
# hum is paused.
TRIM = {"out02", "in01"} | set(ADVERT.values())
MP3_TRACKS = 9


def load(z, name):
    w = wave.open(io.BytesIO(z.read(FONT + name + ".wav")))
    assert w.getnchannels() == 1 and w.getsampwidth() == 2 and w.getframerate() == 44100, name
    return np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").astype(np.float64)


def trim(a, sr=44100):
    """Cut trailing silence (below 3 % of peak, in 20 ms windows) and fade the last 40 ms."""
    win = sr // 50
    env = np.array([np.abs(a[i:i + win]).max() for i in range(0, len(a) - win, win)])
    last = max(i for i, e in enumerate(env) if e > env.max() * 0.03)
    a = a[:(last + 2) * win].copy()
    n = sr // 25
    a[-n:] *= np.linspace(1, 0, n)
    return a


def save(path, a):
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(44100)
        w.writeframes(np.clip(a, -32768, 32767).astype("<i2").tobytes())


def main():
    for folder in ("MP3", "ADVERT"):
        shutil.rmtree(SD / folder, ignore_errors=True)
    make_ui_sounds.main()

    if not CACHE.exists():
        CACHE.parent.mkdir(parents=True, exist_ok=True)
        print("downloading", URL)
        urllib.request.urlretrieve(URL, CACHE)
    z = zipfile.ZipFile(CACHE)
    for folder, table in (("MP3", MP3), ("ADVERT", ADVERT)):
        for track, src in table.items():
            a = load(z, src)
            save(SD / folder / f"{track:04d}.wav", trim(a) if src in TRIM else a)

    # Length of every main-channel track in ms, indexed by track number. The DFPlayer can't play
    # a track once and stop, so the firmware stops each one-shot itself when it ends.
    lengths = [0]
    for t in range(1, MP3_TRACKS + 1):
        with wave.open(str(SD / "MP3" / f"{t:04d}.wav")) as w:
            lengths.append(int(w.getnframes() * 1000 / w.getframerate()))
    (FW / "lightsaber" / "sound_lengths.h").write_text(
        "// Written by firmware/tools/build_sd_card.py. Lengths in ms of the main-channel tracks.\n"
        "#pragma once\n"
        f"#define SND_IGNITE_MS {lengths[3] + 50}\n"
        f"#define SND_RETRACT_MS {lengths[4] + 50}\n"
        f"#define SND_LENGTHS_MS {{{', '.join(map(str, lengths))}}}  // index = track number\n",
        newline="\n")
    print("lengths (ms):", lengths)


if __name__ == "__main__":
    main()
