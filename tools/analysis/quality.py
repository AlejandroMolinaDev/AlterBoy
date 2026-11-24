"""Objective quality metrics for pitch / formant processing.

The reference ("ideal") is the same synthetic voice generated directly with the
target f0 and the target formant scale. Metrics on the steady part:

* ``envelope_lsd_db``: RMS difference (dB) of the cepstrally smoothed spectral
  envelopes between output and ideal, 150 Hz - 5 kHz ("does it sound like the
  same voice / the intended formant shift").
* ``hnr_db``: harmonic-to-noise ratio from the normalised autocorrelation at the
  period (detects buzz / graininess / noise added by the processing).
"""
from __future__ import annotations

import numpy as np

import metrics as m


def _frames(x: np.ndarray, n: int, hop: int):
    for start in range(0, len(x) - n, hop):
        yield x[start:start + n]


def envelope_db(x: np.ndarray, sr: int, nfft: int = 4096, lifter: int = 40) -> tuple[np.ndarray, np.ndarray]:
    """Average cepstrally smoothed log-magnitude spectrum (dB)."""
    x = m.steady(m.mono(x), sr).astype(np.float64)
    w = np.hanning(nfft)
    logs = []
    for f in _frames(x, nfft, nfft // 2):
        spec = np.abs(np.fft.rfft(f * w)) + 1e-9
        logs.append(np.log(spec))
    if not logs:
        return np.array([]), np.array([])
    log_mag = np.mean(logs, axis=0)
    cep = np.fft.irfft(log_mag)
    cep[lifter:-lifter] = 0.0
    smooth = np.fft.rfft(cep).real
    freqs = np.fft.rfftfreq(nfft, 1 / sr)
    return freqs, 20 * smooth / np.log(10)


def envelope_lsd_db(out: np.ndarray, ideal: np.ndarray, sr: int, lo: float = 150.0, hi: float = 5000.0) -> float:
    f, a = envelope_db(out, sr)
    _, b = envelope_db(ideal, sr)
    band = (f >= lo) & (f <= hi)
    a = a[band] - np.mean(a[band])  # level-independent
    b = b[band] - np.mean(b[band])
    return float(np.sqrt(np.mean((a - b) ** 2)))


def hnr_db(x: np.ndarray, sr: int, f0: float) -> float:
    x = m.steady(m.mono(x), sr).astype(np.float64)
    period = sr / f0
    n = int(max(3 * period, 0.04 * sr))
    values = []
    for f in _frames(x, n + int(period * 1.2) + 2, n // 2):
        a = f[:n]
        if np.sqrt(np.mean(a ** 2)) < 1e-4:
            continue
        best = 0.0
        for lag in range(int(period * 0.9), int(period * 1.1) + 1):
            b = f[lag:lag + n]
            r = np.dot(a, b) / (np.sqrt(np.dot(a, a) * np.dot(b, b)) + 1e-12)
            best = max(best, r)
        best = min(best, 0.99999)
        values.append(10 * np.log10(best / (1 - best)) if best > 0 else -10.0)
    return float(np.median(values)) if values else float("nan")


def harmonic_levels(x: np.ndarray, sr: int, f0: float, fmax: float = 5000.0) -> np.ndarray:
    """Levels (dB) of the harmonics k*f0 up to fmax, from a long Hann-windowed FFT."""
    x = m.steady(m.mono(x), sr).astype(np.float64)
    w = np.hanning(len(x))
    mag = np.abs(np.fft.rfft(x * w)) + 1e-12
    freqs = np.fft.rfftfreq(len(x), 1 / sr)
    levels = []
    k = 1
    while k * f0 <= fmax:
        band = (freqs > k * f0 * 0.985) & (freqs < k * f0 * 1.015)
        levels.append(20 * np.log10(mag[band].max()) if np.any(band) else -240.0)
        k += 1
    return np.array(levels)


def harmonic_distance_db(out: np.ndarray, ideal: np.ndarray, sr: int, f0: float) -> float:
    """RMS difference (dB, level-independent) of the harmonic amplitudes output vs ideal,
    ignoring harmonics more than 40 dB below the strongest one in the ideal voice."""
    a, b = harmonic_levels(out, sr, f0), harmonic_levels(ideal, sr, f0)
    n = min(len(a), len(b))
    a, b = a[:n], b[:n]
    keep = b > b.max() - 40.0
    if not np.any(keep):
        return float("nan")
    d = (a[keep] - a[keep].mean()) - (b[keep] - b[keep].mean())
    return float(np.sqrt(np.mean(d ** 2)))
