"""Synthesize the menu, boot and low-battery sounds (the only synthesized tracks).

Called by build_sd_card.py. Needs numpy.
"""
import wave
from pathlib import Path

import numpy as np

SR = 22050
SD = Path(__file__).resolve().parent.parent / "sd-card"


def env_ad(n, attack, decay):
    """Attack, then exponential decay (times in s)."""
    t = np.arange(n) / SR
    return np.clip(t / max(attack, 1e-4), 0, 1) * np.exp(-np.maximum(t - attack, 0) / decay)


def norm(x, peak_db):
    return x / (np.abs(x).max() + 1e-9) * 10 ** (peak_db / 20)


def fade(x, fin=0.004, fout=0.02):
    a, b = int(fin * SR), int(fout * SR)
    x = x.copy()
    x[:a] *= np.linspace(0, 1, a)
    x[-b:] *= np.linspace(1, 0, b)
    return x


def tones(notes, each, gap=0.0, decay=0.25):
    parts = []
    for f in notes:
        n = int(each * SR)
        t = np.arange(n) / SR
        parts.append((np.sin(2 * np.pi * f * t) + 0.3 * np.sin(4 * np.pi * f * t)) * env_ad(n, 0.003, decay))
        if gap:
            parts.append(np.zeros(int(gap * SR)))
    return fade(norm(np.concatenate(parts), -3))


def sweep(f1, f2, dur):
    t = np.arange(int(dur * SR)) / SR
    ph = 2 * np.pi * np.cumsum(f1 * (f2 / f1) ** (t / dur)) / SR
    return fade(norm(np.sin(ph) * env_ad(len(t), 0.004, dur * 0.6), -3))


def boot():
    chime = np.concatenate([tones([660], 0.22), tones([880], 0.22), tones([1320, 1760], 0.4, decay=0.35)])
    return fade(norm(chime, -2))


def write(path, x):
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes((np.clip(x, -1, 1) * 32767).astype("<i2").tobytes())


def main():
    tick = tones([1500], 0.04, decay=0.015)
    for track, x in {1: boot(), 6: tick, 7: tones([880, 1320], 0.08, decay=0.06),
                     8: tones([1320, 660], 0.08, decay=0.06), 9: tones([440, 330], 0.18, gap=0.06, decay=0.12)}.items():
        write(SD / "MP3" / f"{track:04d}.wav", x)
    write(SD / "ADVERT" / "0017.wav", tick)
    write(SD / "ADVERT" / "0018.wav", sweep(300, 1400, 0.18))


if __name__ == "__main__":
    main()
