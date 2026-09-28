#!/usr/bin/env python3
"""Synthesize the default KYBER OS sound font (original, royalty-free) for the DFPlayer.

  python firmware/tools/make_sounds.py

Writes firmware/sd-card/MP3/*.wav, firmware/sd-card/ADVERT/*.wav and
firmware/lightsaber/sound_lengths.h (one-shot lengths the firmware schedules around).
Needs numpy. Everything is generated from oscillators, filtered noise and envelopes;
the RNG is seeded so the font is reproducible.
"""
import wave
from pathlib import Path

import numpy as np

SR = 22050
ROOT = Path(__file__).resolve().parent.parent
SD = ROOT / "sd-card"
rng = np.random.default_rng(1977)

F0 = 88.0  # hum fundamental; 88 Hz * 8 s = whole cycles, so the loop is seamless


# ---------------------------------------------------------------- building blocks

def t_axis(dur):
    return np.arange(int(dur * SR)) / SR


def env_ad(n, attack, decay):
    """Attack then exponential decay (times in s)."""
    t = np.arange(n) / SR
    a = np.clip(t / max(attack, 1e-4), 0, 1)
    return a * np.exp(-np.maximum(t - attack, 0) / decay)


def bump(n, peak=0.35, sharp=2.0):
    """Smooth 0 -> 1 -> 0 envelope peaking at `peak` (fraction of n)."""
    x = np.linspace(0, 1, n)
    rise = np.sin(np.clip(x / peak, 0, 1) * np.pi / 2) ** sharp
    fall = np.cos(np.clip((x - peak) / (1 - peak), 0, 1) * np.pi / 2) ** sharp
    return np.where(x < peak, rise, fall)


def svf(x, fc, q=0.7, mode="bp"):
    """State-variable filter with per-sample cutoff (array or scalar)."""
    fc = np.broadcast_to(np.asarray(fc, float), x.shape)
    damp = 1.0 / q
    fmax = (-damp + np.sqrt(damp * damp + 4)) * 0.9  # Chamberlin SVF stability limit
    f = np.minimum(2 * np.sin(np.pi * np.clip(fc, 20, SR * 0.2) / SR), fmax)
    lo = bp = 0.0
    out = np.empty_like(x)
    for i in range(len(x)):
        hi = x[i] - lo - damp * bp
        bp += f[i] * hi
        lo += f[i] * bp
        out[i] = bp if mode == "bp" else lo if mode == "lp" else hi
    return out


def circ_noise(n, lo, hi):
    """Band-limited noise that loops seamlessly (shaped in the frequency domain)."""
    spec = np.fft.rfft(rng.standard_normal(n))
    freqs = np.fft.rfftfreq(n, 1 / SR)
    mask = (freqs >= lo) & (freqs <= hi)
    spec *= mask
    x = np.fft.irfft(spec, n)
    return x / (np.abs(x).max() + 1e-9)


HARM = [1.0, 0.62, 0.38, 0.52, 0.22, 0.17, 0.11, 0.08, 0.05]


def hum_voice(freq, phase0=0.0):
    """Harmonic saber tone following an instantaneous frequency curve (Hz array)."""
    ph = phase0 + 2 * np.pi * np.cumsum(freq) / SR
    out = np.zeros_like(ph)
    for k, a in enumerate(HARM, 1):
        out += a * np.sin(k * ph + k * 0.7)
    # a little odd-order grit, like a projector motor
    out += 0.12 * np.sign(np.sin(2 * ph)) * np.abs(np.sin(2 * ph)) ** 3
    return out / 2.6


def make_loop(x, fade):
    """Crossfade the tail into the head so the clip loops."""
    f = int(fade * SR)
    body, tail = x[:-f].copy(), x[-f:]
    r = np.linspace(0, 1, f)
    body[:f] = body[:f] * r + tail * (1 - r)
    return body


def norm(x, peak_db=-1.0):
    return x / (np.abs(x).max() + 1e-9) * 10 ** (peak_db / 20)


def loud(x, rms=0.2):
    """Normalise by loudness (RMS) with a soft clip, so spiky sounds aren't quiet."""
    x = x / (np.sqrt((x ** 2).mean()) + 1e-9) * rms
    return np.tanh(x * 1.4) / 1.4 * 1.05


def fade_edges(x, fin=0.004, fout=0.02):
    a, b = int(fin * SR), int(fout * SR)
    x = x.copy()
    if a:
        x[:a] *= np.linspace(0, 1, a)
    if b:
        x[-b:] *= np.linspace(1, 0, b)
    return x


def write(path, x):
    path.parent.mkdir(parents=True, exist_ok=True)
    pcm = (np.clip(x, -1, 1) * 32767).astype("<i2")
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(pcm.tobytes())
    return int(len(x) * 1000 / SR)


# ---------------------------------------------------------------- the font

def hum(dur=8.0):
    t = t_axis(dur)
    wob = 1 + 0.004 * np.sin(2 * np.pi * 0.25 * t)            # 2 cycles in 8 s
    x = hum_voice(F0 * wob)
    am = 1 + 0.07 * np.sin(2 * np.pi * 0.75 * t) + 0.03 * np.sin(2 * np.pi * 3.0 * t)
    x = x * am + 0.10 * circ_noise(len(t), 70, 700)
    return norm(x, -7)


def swing(fast, variant):
    dur = (0.36 + 0.05 * variant) if fast else (0.55 + 0.07 * variant)
    n = int(dur * SR)
    e = bump(n, peak=0.3 if fast else 0.4, sharp=1.6)
    depth = (0.55 + 0.08 * variant) if fast else (0.22 + 0.06 * variant)
    tone = hum_voice(F0 * (1 + depth * e)) * (0.6 + 0.9 * e)
    fc = (350 + (2400 if fast else 1100) * e) * (1 + 0.1 * variant)
    whoosh = svf(rng.standard_normal(n), fc, q=1.4) * e * (0.9 if fast else 0.5)
    return fade_edges(norm(tone + whoosh, -2))


def clash(variant):
    dur = 0.75
    n = int(dur * SR)
    t = t_axis(dur)
    burst = rng.standard_normal(n) * env_ad(n, 0.001, 0.018)
    ring = np.zeros(n)
    base = [523, 1187, 1781, 2633, 3407, 4561]
    for k, f in enumerate(base):
        f *= 1 + 0.04 * (variant - 1.5) + rng.uniform(-0.02, 0.02)
        ring += np.sin(2 * np.pi * f * t + rng.uniform(0, 6)) * env_ad(n, 0.0005, 0.06 + 0.05 * (k % 3)) / (1 + k * 0.4)
    crack = svf(rng.standard_normal(n) * (rng.random(n) < 0.02), 2500, q=2) * env_ad(n, 0.002, 0.25) * 6
    dip = hum_voice(F0 * (1 - 0.35 * env_ad(n, 0.002, 0.12))) * env_ad(n, 0.004, 0.3)
    return fade_edges(loud(burst * 1.2 + ring * 0.9 + crack + dip * 0.7, 0.24), 0.0005, 0.05)


def blaster(variant):
    dur = 0.55
    n = int(dur * SR)
    t = t_axis(dur)
    f = 2600 * (180 / 2600) ** np.clip(t / (0.22 + 0.03 * variant), 0, 1)
    f *= 1 + 0.08 * np.sin(2 * np.pi * (35 + 6 * variant) * t)
    ph = 2 * np.pi * np.cumsum(f) / SR
    zap = (np.sin(ph) + 0.3 * np.sign(np.sin(ph))) * env_ad(n, 0.002, 0.12)
    hit = rng.standard_normal(n) * env_ad(n, 0.0005, 0.02)
    echo = np.zeros(n)
    d = int(0.09 * SR)
    echo[d:] = zap[:-d] * 0.35
    return fade_edges(loud(zap + hit * 0.6 + echo, 0.24), 0.0005)


def stab():
    n = int(0.6 * SR)
    e = bump(n, 0.2, 1.3)
    tone = hum_voice(F0 * (1 + 0.8 * e)) * (0.5 + e)
    thrust = svf(rng.standard_normal(n), 600 + 2500 * e, q=1.2) * e
    tail = clash(0)[:n] * np.clip((np.arange(n) - n * 0.25) / (n * 0.1), 0, 1) * 0.6
    return fade_edges(norm(tone + thrust + tail, -1))


def force():
    n = int(1.3 * SR)
    e = bump(n, 0.45, 1.2)
    rumble = svf(rng.standard_normal(n), 90 + 200 * e, q=0.9, mode="lp") * 3
    air = svf(rng.standard_normal(n), 300 + 900 * e, q=0.8) * 0.6
    tone = hum_voice(F0 * (1 - 0.25 * e)) * 0.5
    return fade_edges(norm((rumble + air) * e + tone, -1))


def ignite():
    dur = 1.1
    n = int(dur * SR)
    t = t_axis(dur)
    x = t / dur
    # pitch shoots up past the hum, then settles onto it
    f = F0 * (0.35 + 1.25 * np.clip(x / 0.35, 0, 1) ** 0.6 - 0.25 * np.clip((x - 0.35) / 0.65, 0, 1))
    tone = hum_voice(f) * np.clip(x / 0.05, 0, 1)
    crackle = svf(rng.standard_normal(n) * (rng.random(n) < 0.05), 3000, q=2) * env_ad(n, 0.001, 0.12) * 5
    whoosh = svf(rng.standard_normal(n), 200 + 2500 * bump(n, 0.25), q=1.0) * bump(n, 0.25) * 0.8
    thump = np.sin(2 * np.pi * 55 * t) * env_ad(n, 0.002, 0.08)
    body = tone * (1 + 0.6 * bump(n, 0.3)) + crackle + whoosh + thump
    return fade_edges(loud(body, 0.2), 0.001, 0.03)


def retract():
    dur = 1.0
    n = int(dur * SR)
    t = t_axis(dur)
    x = t / dur
    f = F0 * (1.1 - 0.85 * x ** 0.7)
    tone = hum_voice(f) * (1 - x) ** 1.4
    whoosh = svf(rng.standard_normal(n), 2200 * (1 - x) + 150, q=1.0) * bump(n, 0.15) * 0.7
    fizz = svf(rng.standard_normal(n) * (rng.random(n) < 0.03), 3500, q=2) * (1 - x) ** 3 * 3
    return fade_edges(loud(tone * 1.2 + whoosh + fizz, 0.17), 0.002, 0.05)


def loop_fx(kind, dur=3.0):
    n = int((dur + 0.15) * SR)
    t = np.arange(n) / SR
    base = hum_voice(F0 * (1 + 0.02 * np.sin(2 * np.pi * 5 * t))) * 0.5
    if kind == "lockup":
        pops = rng.standard_normal(n) * (rng.random(n) < 0.08)
        crack = svf(pops, 1800 + 1200 * rng.random(n), q=1.5) * 5
        buzz = np.sign(np.sin(2 * np.pi * 47 * t)) * (0.5 + 0.5 * svf(rng.standard_normal(n), 12, q=0.5, mode="lp") * 8)
        x = base + crack + 0.25 * buzz
    elif kind == "drag":
        scrape = svf(rng.standard_normal(n), 3000, q=0.8) * (0.6 + 0.4 * np.abs(svf(rng.standard_normal(n), 15, mode="lp")) * 20)
        sparks = svf(rng.standard_normal(n) * (rng.random(n) < 0.05), 5000, q=2) * 4
        x = base * 0.6 + scrape + sparks
    elif kind == "melt":
        boil = svf(rng.standard_normal(n), 400, q=0.7, mode="lp") * (1 + np.sin(2 * np.pi * 7 * t + 3 * np.sin(2 * np.pi * 0.9 * t)))
        hiss = svf(rng.standard_normal(n), 6000, q=0.6) * 0.5
        x = base * 0.7 + boil * 1.5 + hiss
    else:  # lightning
        gate = (svf(rng.standard_normal(n), 25, q=0.5, mode="lp") > 0.0).astype(float)
        arc = np.sign(np.sin(2 * np.pi * 120 * t)) * 0.5 + svf(rng.standard_normal(n), 2500, q=1) * 1.5
        zaps = svf(rng.standard_normal(n) * (rng.random(n) < 0.03), 4000, q=3) * 5
        x = base * 0.4 + arc * (0.3 + 0.7 * gate) + zaps
    return loud(make_loop(x, 0.15), 0.17)


def tone_seq(notes, dur_each, harm=0.3, gap=0.0, decay=0.25):
    parts = []
    for f in notes:
        n = int(dur_each * SR)
        t = np.arange(n) / SR
        s = (np.sin(2 * np.pi * f * t) + harm * np.sin(4 * np.pi * f * t)) * env_ad(n, 0.003, decay)
        parts.append(s)
        if gap:
            parts.append(np.zeros(int(gap * SR)))
    return fade_edges(norm(np.concatenate(parts), -3))


def sweep(f1, f2, dur, square=0.0):
    t = t_axis(dur)
    f = f1 * (f2 / f1) ** (t / dur)
    ph = 2 * np.pi * np.cumsum(f) / SR
    s = np.sin(ph) + square * np.sign(np.sin(ph))
    return fade_edges(norm(s * env_ad(len(t), 0.004, dur * 0.6), -3))


def boot():
    parts = [tone_seq([660], 0.22), tone_seq([880], 0.22), tone_seq([1320, 1760], 0.4, decay=0.35)]
    chime = np.concatenate(parts)
    n = len(chime)
    swell = hum_voice(np.full(n, F0 * 2)) * bump(n, 0.8) * 0.3
    return fade_edges(norm(chime + swell, -2))


def main():
    lengths = {}
    mp3 = {
        1: ("boot", boot()),
        2: ("hum", hum()),
        3: ("ignite", ignite()),
        4: ("retract", retract()),
        5: ("lockup", loop_fx("lockup")),
        6: ("drag", loop_fx("drag")),
        7: ("melt", loop_fx("melt")),
        8: ("lightning", loop_fx("lightning")),
        9: ("ui tick", tone_seq([1500], 0.04, decay=0.015)),
        10: ("ui ok", tone_seq([880, 1320], 0.08, decay=0.06)),
        11: ("ui back", tone_seq([1320, 660], 0.08, decay=0.06)),
        12: ("low battery", tone_seq([440, 330], 0.18, gap=0.06, decay=0.12)),
        13: ("sleep", sweep(800, 180, 0.6)),
    }
    adv = {}
    for v in range(4):
        adv[1 + v] = ("swing slow", swing(False, v))
        adv[5 + v] = ("swing fast", swing(True, v))
        adv[9 + v] = ("clash", clash(v))
        adv[13 + v] = ("blaster", blaster(v))
    adv[17] = ("stab", stab())
    adv[18] = ("force", force())
    adv[19] = ("tick", tone_seq([1500], 0.04, decay=0.015))
    adv[20] = ("training shot", sweep(1900, 420, 0.16, square=0.4))
    adv[21] = ("training hit", sweep(170, 120, 0.4, square=1.0))
    adv[22] = ("training over", tone_seq([660, 520, 390], 0.18, decay=0.14))
    adv[23] = ("preset", sweep(300, 1400, 0.18))
    adv[24] = ("ok", tone_seq([880, 1320], 0.08, decay=0.06))

    for num, (name, x) in mp3.items():
        lengths[name] = write(SD / "MP3" / f"{num:04d}.wav", x)
    for num, (name, x) in adv.items():
        write(SD / "ADVERT" / f"{num:04d}.wav", x)

    hdr = ROOT / "lightsaber" / "sound_lengths.h"
    hdr.write_text(
        "// Generated by firmware/tools/make_sounds.py - lengths (ms) of the one-shot main tracks.\n"
        "// Replace with your own font's values if you swap the sound files.\n"
        "#pragma once\n"
        f"#define SND_IGNITE_MS {lengths['ignite']}\n"
        f"#define SND_RETRACT_MS {lengths['retract']}\n"
        f"#define SND_BOOT_MS {lengths['boot']}\n", newline="\n")
    print(f"wrote {len(mp3)} main + {len(adv)} advert tracks to {SD}")
    for num, (name, _) in mp3.items():
        print(f"  MP3/{num:04d}.wav  {name}")
    for num, (name, _) in adv.items():
        print(f"  ADVERT/{num:04d}.wav  {name}")


if __name__ == "__main__":
    main()
