"""Synthetic test signals for Metamorfo (SPEC §7).

All generators return float32 numpy arrays in [-1, 1]. Deterministic (fixed seeds).
"""
from __future__ import annotations

import numpy as np
from scipy import signal as sps

A4 = 440.0


def midi_to_hz(note: float) -> float:
    return A4 * 2.0 ** ((note - 69.0) / 12.0)


def sine(freq: float, seconds: float, sr: int, amp: float = 0.5) -> np.ndarray:
    t = np.arange(int(seconds * sr)) / sr
    return (amp * np.sin(2 * np.pi * freq * t)).astype(np.float32)


def detuned_sine(note: int, cents: float, seconds: float, sr: int, amp: float = 0.5) -> np.ndarray:
    return sine(midi_to_hz(note) * 2.0 ** (cents / 1200.0), seconds, sr, amp)


def silence(seconds: float, sr: int) -> np.ndarray:
    return np.zeros(int(seconds * sr), dtype=np.float32)


def white_noise(seconds: float, sr: int, amp: float = 0.3, seed: int = 1) -> np.ndarray:
    rng = np.random.default_rng(seed)
    return (amp * rng.uniform(-1, 1, int(seconds * sr))).astype(np.float32)


def full_scale(seconds: float, sr: int, freq: float = 997.0) -> np.ndarray:
    """0 dBFS sine (peak exactly 1.0)."""
    x = np.sin(2 * np.pi * freq * np.arange(int(seconds * sr)) / sr)
    return (x / np.max(np.abs(x))).astype(np.float32)


def denormal_noise(seconds: float, sr: int, seed: int = 2) -> np.ndarray:
    """Noise made only of subnormal float32 values (|x| < FLT_MIN)."""
    rng = np.random.default_rng(seed)
    tiny = np.finfo(np.float32).tiny  # 1.17549435e-38
    return (rng.uniform(-1, 1, int(seconds * sr)) * tiny * 0.5).astype(np.float32)


def impulse(seconds: float, sr: int, at: float = 0.25) -> np.ndarray:
    x = np.zeros(int(seconds * sr), dtype=np.float32)
    x[int(at * sr)] = 0.9
    return x


# --- synthetic voice: Rosenberg glottal pulse + formant resonators ------------------

VOWEL_A = [(730, 80), (1090, 90), (2440, 120), (3400, 130)]  # (formant Hz, bandwidth Hz)


def _glottal_train(f0: np.ndarray, sr: int, open_q: float = 0.6, close_q: float = 0.25) -> np.ndarray:
    """Rosenberg-type glottal flow derivative driven by a per-sample f0 curve."""
    phase = np.cumsum(f0 / sr) % 1.0
    tp, tn = open_q, close_q
    flow = np.where(
        phase < tp,
        0.5 * (1 - np.cos(np.pi * phase / tp)),
        np.where(phase < tp + tn, np.cos(np.pi * (phase - tp) / (2 * tn)), 0.0),
    )
    return np.diff(flow, prepend=flow[0])


def _formant_filter(x: np.ndarray, sr: int, formants=VOWEL_A) -> np.ndarray:
    y = np.zeros_like(x)
    for i, (fc, bw) in enumerate(formants):
        r = np.exp(-np.pi * bw / sr)
        theta = 2 * np.pi * fc / sr
        b = [1 - r]
        a = [1, -2 * r * np.cos(theta), r * r]
        y += sps.lfilter(b, a, x) * (0.8 ** i)
    return y


def synthetic_voice(f0_curve: np.ndarray, sr: int, vibrato_hz: float = 5.5,
                    vibrato_cents: float = 0.0, jitter: float = 0.0, seed: int = 3) -> np.ndarray:
    rng = np.random.default_rng(seed)
    t = np.arange(len(f0_curve)) / sr
    f0 = f0_curve * 2.0 ** (vibrato_cents / 1200.0 * np.sin(2 * np.pi * vibrato_hz * t))
    if jitter > 0:
        f0 = f0 * (1 + jitter * sps.lfilter([0.01], [1, -0.99], rng.standard_normal(len(f0))))
    y = _formant_filter(_glottal_train(f0, sr), sr)
    y /= np.max(np.abs(y)) + 1e-12
    return (0.5 * y).astype(np.float32)


def voice_note(freq: float, seconds: float, sr: int, **kw) -> np.ndarray:
    """Steady synthetic vowel /a/ (no vibrato by default -> measurable f0)."""
    return synthetic_voice(np.full(int(seconds * sr), freq), sr, **kw)


def voice_phrase(sr: int) -> np.ndarray:
    """Short melody with vibrato and silences between notes (voice/silence transitions)."""
    notes = [(57, 0.6), (None, 0.25), (60, 0.5), (62, 0.5), (None, 0.3), (64, 0.8), (None, 0.4)]
    parts = []
    for note, dur in notes:
        n = int(dur * sr)
        if note is None:
            parts.append(np.zeros(n, dtype=np.float32))
            continue
        v = synthetic_voice(np.full(n, midi_to_hz(note)), sr, vibrato_cents=25.0, jitter=0.3)
        env = np.ones(n, dtype=np.float32)
        ramp = int(0.02 * sr)
        env[:ramp] = np.linspace(0, 1, ramp)
        env[-ramp:] = np.linspace(1, 0, ramp)
        parts.append(v * env)
    return np.concatenate(parts).astype(np.float32)
