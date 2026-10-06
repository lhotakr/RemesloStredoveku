#!/usr/bin/env python3
"""Generate an original, deterministic smithing sound set for Remeslo stredoveku."""

from __future__ import annotations

import json
import math
import subprocess
import wave
from pathlib import Path

import numpy as np
from scipy import signal


SR = 48_000
ROOT = Path(__file__).resolve().parents[1]
WAV_DIR = ROOT / "masters_wav"
OGG_DIR = ROOT / "assets" / "audio" / "smithing" / "nail"
PREVIEW_DIR = ROOT / "preview"


def envelope(length: int, attack: float, decay: float) -> np.ndarray:
    t = np.arange(length, dtype=np.float64) / SR
    return np.minimum(1.0, t / max(attack, 1.0 / SR)) * np.exp(-t / decay)


def filtered_noise(rng: np.random.Generator, length: int, low: float, high: float) -> np.ndarray:
    noise = rng.normal(0.0, 1.0, length)
    nyquist = SR * 0.5
    low_n = max(20.0, low) / nyquist
    high_n = min(nyquist - 50.0, high) / nyquist
    sos = signal.butter(3, [low_n, high_n], btype="bandpass", output="sos")
    return signal.sosfilt(sos, noise)


def normalize(samples: np.ndarray, peak_db: float = -2.5) -> np.ndarray:
    samples = np.asarray(samples, dtype=np.float64)
    samples -= float(np.mean(samples))
    peak = float(np.max(np.abs(samples)))
    if peak > 1e-9:
        samples *= (10.0 ** (peak_db / 20.0)) / peak
    return np.clip(samples, -1.0, 1.0)


def write_wav(path: Path, samples: np.ndarray) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    pcm = (normalize(samples) * 32767.0).astype("<i2")
    with wave.open(str(path), "wb") as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(SR)
        out.writeframes(pcm.tobytes())


def impact(seed: int, heavy: bool, cold: bool = False) -> np.ndarray:
    rng = np.random.default_rng(seed)
    duration = 0.72 if heavy else 0.48
    if cold:
        duration += 0.22
    n = int(duration * SR)
    t = np.arange(n) / SR

    base = rng.uniform(172.0, 205.0)
    if heavy:
        modes = [(0.55, 0.060), (1.0, 0.105), (2.63, 0.18), (4.18, 0.24), (7.7, 0.15)]
    else:
        modes = [(0.8, 0.045), (1.0, 0.085), (2.71, 0.14), (4.45, 0.19), (8.1, 0.12)]
    if cold:
        modes += [(10.7, 0.32), (14.2, 0.27)]

    y = np.zeros(n)
    for mul, decay in modes:
        phase = rng.uniform(0.0, math.tau)
        detune = rng.uniform(0.985, 1.015)
        ring = np.sin(math.tau * base * mul * detune * t + phase)
        y += rng.uniform(0.28, 0.65) * ring * envelope(n, 0.0008, decay)

    transient = filtered_noise(rng, n, 180.0, 9_500.0)
    transient *= envelope(n, 0.00025, 0.018 if heavy else 0.012)
    body = filtered_noise(rng, n, 70.0, 1_600.0)
    body *= envelope(n, 0.001, 0.055 if heavy else 0.035)

    hot_damping = 0.72 if not cold else 1.0
    y = hot_damping * y + 1.05 * transient + (0.72 if heavy else 0.42) * body
    return normalize(np.tanh(1.15 * y))


def metal_click(seed: int, double: bool = True) -> np.ndarray:
    rng = np.random.default_rng(seed)
    n = int(0.24 * SR)
    y = np.zeros(n)
    starts = [0.0, rng.uniform(0.027, 0.045)] if double else [0.0]
    for start in starts:
        start_i = int(start * SR)
        remain = n - start_i
        t = np.arange(remain) / SR
        f = rng.uniform(1_150.0, 1_650.0)
        click = (
            np.sin(math.tau * f * t)
            + 0.55 * np.sin(math.tau * f * 2.73 * t + 0.4)
            + 0.25 * filtered_noise(rng, remain, 1_200.0, 11_000.0)
        )
        y[start_i:] += rng.uniform(0.65, 1.0) * click * envelope(remain, 0.0004, 0.020)
    return normalize(np.tanh(y))


def tongs_rotate(seed: int) -> np.ndarray:
    rng = np.random.default_rng(seed)
    duration = 0.46
    n = int(duration * SR)
    t = np.arange(n) / SR
    scrape = filtered_noise(rng, n, 950.0, 7_000.0)
    # The file starts on grip_closed. The quiet lead-in bridges the visual lift;
    # the scrape itself begins when the atlas reaches roll_begin.
    motion = np.sin(np.pi * np.clip((t - 0.14) / 0.30, 0.0, 1.0)) ** 1.4
    scrape *= motion * (0.32 + 0.12 * np.sin(math.tau * 21.0 * t))
    tone = signal.chirp(t, f0=720.0, f1=410.0, t1=duration, method="quadratic")
    tone *= motion * 0.16
    return normalize(scrape + tone, -4.0)


def scale_flakes(seed: int) -> np.ndarray:
    rng = np.random.default_rng(seed)
    n = int(0.37 * SR)
    y = np.zeros(n)
    for start in rng.uniform(0.0, 0.24, 7):
        pos = int(start * SR)
        remain = n - pos
        t = np.arange(remain) / SR
        freq = rng.uniform(2_300.0, 6_800.0)
        tick = np.sin(math.tau * freq * t + rng.uniform(0, math.tau))
        tick += 0.35 * filtered_noise(rng, remain, 3_000.0, 12_000.0)
        y[pos:] += rng.uniform(0.15, 0.38) * tick * envelope(remain, 0.0002, rng.uniform(0.008, 0.023))
    return normalize(y, -8.0)


def quench(seed: int, long: bool) -> np.ndarray:
    rng = np.random.default_rng(seed)
    duration = 2.30 if long else 1.35
    n = int(duration * SR)
    t = np.arange(n) / SR
    steam = filtered_noise(rng, n, 600.0, 11_000.0)
    hiss_env = np.minimum(1.0, t / 0.022) * np.exp(-t / (0.72 if long else 0.44))
    hiss_env *= 0.85 + 0.15 * np.sin(math.tau * 7.0 * t)

    water = filtered_noise(rng, n, 70.0, 820.0)
    water *= np.minimum(1.0, t / 0.008) * np.exp(-t / (0.38 if long else 0.26))

    bubbles = np.zeros(n)
    count = 34 if long else 18
    for start in rng.uniform(0.04, duration * 0.82, count):
        pos = int(start * SR)
        remain = n - pos
        bt = np.arange(remain) / SR
        freq = rng.uniform(95.0, 270.0)
        bubble = np.sin(math.tau * freq * bt) * envelope(remain, 0.001, rng.uniform(0.012, 0.035))
        bubbles[pos:] += bubble * rng.uniform(0.08, 0.25)

    return normalize(0.95 * steam * hiss_env + 0.52 * water + bubbles)


def bellows(seed: int) -> np.ndarray:
    rng = np.random.default_rng(seed)
    duration = 1.18
    n = int(duration * SR)
    t = np.arange(n) / SR
    air = filtered_noise(rng, n, 55.0, 1_950.0)
    swell = np.sin(np.pi * np.clip(t / duration, 0.0, 1.0)) ** 1.35
    leather = filtered_noise(rng, n, 110.0, 620.0) * (0.18 + 0.08 * np.sin(math.tau * 4.5 * t))
    y = air * swell + leather * swell
    y += 0.20 * np.sin(math.tau * 78.0 * t) * swell
    return normalize(y, -5.0)


def forge_loop(seed: int, duration: float = 6.0) -> np.ndarray:
    rng = np.random.default_rng(seed)
    n = int(duration * SR)
    t = np.arange(n) / SR

    raw = filtered_noise(rng, n, 80.0, 3_600.0)
    crackle = np.zeros(n)
    for start in rng.uniform(0.0, duration, 54):
        pos = int(start * SR)
        remain = n - pos
        ct = np.arange(remain) / SR
        pulse = filtered_noise(rng, remain, 1_700.0, 12_500.0)
        pulse *= envelope(remain, 0.0002, rng.uniform(0.004, 0.024))
        crackle[pos:] += rng.uniform(0.12, 0.48) * pulse

    breathe = 0.52 + 0.18 * np.sin(math.tau * t / 2.35) + 0.08 * np.sin(math.tau * t / 0.71)
    y = 0.24 * raw * breathe + crackle

    cross = int(0.45 * SR)
    fade = np.linspace(0.0, 1.0, cross)
    mixed = y[-cross:] * (1.0 - fade) + y[:cross] * fade
    y[:cross] = mixed
    y[-cross:] = mixed
    return normalize(y, -10.0)


def ambience(seed: int, duration: float = 8.0) -> np.ndarray:
    rng = np.random.default_rng(seed)
    n = int(duration * SR)
    t = np.arange(n) / SR
    room = filtered_noise(rng, n, 75.0, 1_100.0) * 0.08
    room *= 0.75 + 0.25 * np.sin(math.tau * t / 3.7)
    fire = forge_loop(seed + 1, duration)
    y = room + 0.72 * fire
    return normalize(y, -12.0)


def encode_ogg(wav_path: Path, ogg_path: Path) -> None:
    ogg_path.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        ["ffmpeg", "-y", "-loglevel", "error", "-i", str(wav_path), "-c:a", "libvorbis", "-q:a", "5", str(ogg_path)],
        check=True,
    )


def main() -> None:
    WAV_DIR.mkdir(parents=True, exist_ok=True)
    OGG_DIR.mkdir(parents=True, exist_ok=True)
    PREVIEW_DIR.mkdir(parents=True, exist_ok=True)

    sounds: dict[str, np.ndarray] = {}
    for i in range(4):
        sounds[f"nail_impact_hot_light_{i + 1:02d}"] = impact(100 + i, heavy=False)
    for i in range(3):
        sounds[f"nail_impact_hot_heavy_{i + 1:02d}"] = impact(200 + i, heavy=True)
    for i in range(2):
        sounds[f"nail_impact_cooling_{i + 1:02d}"] = impact(300 + i, heavy=False, cold=True)
    for i in range(2):
        sounds[f"tongs_grip_{i + 1:02d}"] = metal_click(400 + i)
        sounds[f"tongs_rotate_quarter_{i + 1:02d}"] = tongs_rotate(500 + i)
    sounds["tongs_settle_01"] = metal_click(550, double=False) * 0.72
    sounds["scale_flakes_01"] = scale_flakes(600)
    sounds["quench_short_01"] = quench(700, long=False)
    sounds["quench_heavy_01"] = quench(701, long=True)
    sounds["bellows_push_01"] = bellows(800)
    sounds["forge_fire_loop_01"] = forge_loop(900)
    sounds["smithy_room_loop_01"] = ambience(901)

    metadata = []
    for sound_id, samples in sounds.items():
        wav_path = WAV_DIR / f"{sound_id}.wav"
        ogg_path = OGG_DIR / f"{sound_id}.ogg"
        write_wav(wav_path, samples)
        encode_ogg(wav_path, ogg_path)
        metadata.append({
            "id": sound_id,
            "duration_ms": int(round(len(samples) * 1000 / SR)),
            "sample_rate": SR,
            "channels": 1,
            "wav": str(wav_path.relative_to(ROOT)).replace("\\", "/"),
            "ogg": str(ogg_path.relative_to(ROOT)).replace("\\", "/"),
        })

    silence = np.zeros(int(0.34 * SR))
    preview_order = [
        "smithy_room_loop_01",
        "bellows_push_01",
        "nail_impact_hot_light_01",
        "nail_impact_hot_light_02",
        "nail_impact_hot_heavy_01",
        "tongs_grip_01",
        "tongs_rotate_quarter_01",
        "tongs_settle_01",
        "scale_flakes_01",
        "quench_short_01",
        "quench_heavy_01",
    ]
    demo = np.concatenate([np.concatenate([sounds[sound_id], silence]) for sound_id in preview_order])
    demo_wav = PREVIEW_DIR / "smithing_nail_audio_demo.wav"
    demo_ogg = PREVIEW_DIR / "smithing_nail_audio_demo.ogg"
    write_wav(demo_wav, demo)
    encode_ogg(demo_wav, demo_ogg)

    (ROOT / "generated_audio_metadata.json").write_text(
        json.dumps({"sample_rate": SR, "sounds": metadata}, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
