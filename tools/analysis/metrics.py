"""Measurement functions for the Metamorfo audit (pitch, formant, harmonics, latency, clicks)."""
from __future__ import annotations

import numpy as np


def cents(measured: float, reference: float) -> float:
    return 1200.0 * np.log2(measured / reference)


def mono(x: np.ndarray) -> np.ndarray:
    return x if x.ndim == 1 else x[:, 0]


def steady(x: np.ndarray, sr: int, skip: float = 0.2) -> np.ndarray:
    """Drops onset/offset so measurements see the steady state."""
    n = int(skip * sr)
    return x[n:-n] if len(x) > 3 * n else x


def sine_freq(x: np.ndarray, sr: int) -> float:
    """High-resolution frequency of the strongest partial (zero-padded FFT + parabolic interp)."""
    x = steady(mono(x), sr)
    w = x * np.hanning(len(x))
    nfft = 1 << int(np.ceil(np.log2(len(w) * 16)))
    mag = np.abs(np.fft.rfft(w, nfft))
    k = int(np.argmax(mag[1:])) + 1
    a, b, c = np.log(mag[k - 1] + 1e-30), np.log(mag[k] + 1e-30), np.log(mag[k + 1] + 1e-30)
    p = 0.5 * (a - c) / (a - 2 * b + c)
    return (k + p) * sr / nfft


def yin_f0(x: np.ndarray, sr: int, fmin: float = 60.0, fmax: float = 1200.0,
           frame: float = 0.05, hop: float = 0.01, threshold: float = 0.12) -> float:
    """Median YIN f0 over the steady part (reference estimator for harmonic signals)."""
    x = steady(mono(x), sr).astype(np.float64)
    n, h = int(frame * sr), int(hop * sr)
    tau_min, tau_max = int(sr / fmax), int(sr / fmin)
    estimates = []
    for start in range(0, len(x) - n - tau_max, h):
        seg = x[start:start + n + tau_max]
        if np.sqrt(np.mean(seg ** 2)) < 1e-4:
            continue
        d = np.array([np.sum((seg[:n] - seg[t:t + n]) ** 2) for t in range(tau_max + 1)])
        cmnd = d.copy()
        cmnd[0] = 1.0
        cmnd[1:] = d[1:] * np.arange(1, tau_max + 1) / (np.cumsum(d[1:]) + 1e-30)
        cand = np.where(cmnd[tau_min:] < threshold)[0]
        t = (cand[0] + tau_min) if len(cand) else int(np.argmin(cmnd[tau_min:]) + tau_min)
        while t + 1 <= tau_max and cmnd[t + 1] < cmnd[t]:
            t += 1
        if 1 <= t < tau_max:
            a, b, c = cmnd[t - 1], cmnd[t], cmnd[t + 1]
            denom = a - 2 * b + c
            t = t + (0.5 * (a - c) / denom if denom != 0 else 0.0)
        estimates.append(sr / t)
    return float(np.median(estimates)) if estimates else float("nan")


def spectral_centroid(x: np.ndarray, sr: int) -> float:
    x = steady(mono(x), sr)
    mag = np.abs(np.fft.rfft(x * np.hanning(len(x))))
    freqs = np.fft.rfftfreq(len(x), 1 / sr)
    return float(np.sum(freqs * mag) / (np.sum(mag) + 1e-30))


def harmonic_levels_db(x: np.ndarray, sr: int, f0: float, count: int = 5) -> list[float]:
    """Levels of H1..Hcount in dBFS (peak magnitude near each harmonic)."""
    x = steady(mono(x), sr)
    w = np.hanning(len(x))
    mag = np.abs(np.fft.rfft(x * w)) * 2 / np.sum(w)
    freqs = np.fft.rfftfreq(len(x), 1 / sr)
    out = []
    for k in range(1, count + 1):
        band = (freqs > k * f0 * 0.98) & (freqs < k * f0 * 1.02)
        out.append(float(20 * np.log10(np.max(mag[band]) + 1e-12)) if np.any(band) else float("nan"))
    return out


def rms_db(x: np.ndarray) -> float:
    return float(20 * np.log10(np.sqrt(np.mean(np.asarray(x, np.float64) ** 2)) + 1e-12))


def latency_samples(dry: np.ndarray, wet: np.ndarray) -> int:
    """Delay of wet relative to dry by cross-correlation (positive = wet is late)."""
    a, b = mono(dry).astype(np.float64), mono(wet).astype(np.float64)
    n = 1 << int(np.ceil(np.log2(len(a) + len(b))))
    corr = np.fft.irfft(np.fft.rfft(b, n) * np.conj(np.fft.rfft(a, n)), n)
    lag = int(np.argmax(np.abs(corr)))
    return lag if lag < n // 2 else lag - n


def null_db(reference: np.ndarray, test: np.ndarray, delay: int = 0) -> float:
    """Residual peak (dBFS) after aligning test by `delay` samples. -inf = perfect null."""
    r = mono(reference).astype(np.float64)
    t = mono(test).astype(np.float64)[delay:delay + len(r)]
    r = r[:len(t)]
    peak = np.max(np.abs(r - t)) if len(t) else 1.0
    return float(20 * np.log10(peak)) if peak > 0 else float("-inf")


def clicks(x: np.ndarray, sr: int, threshold_db: float = 30.0) -> int:
    """Counts discontinuities: second-difference spikes far above the local level."""
    x = mono(x).astype(np.float64)
    d2 = np.abs(np.diff(x, 2))
    win = max(1, int(0.005 * sr))
    local = np.convolve(d2, np.ones(win) / win, mode="same") + 1e-9
    spikes = d2 > local * 10 ** (threshold_db / 20)
    spikes &= d2 > 1e-3
    # Merge spikes closer than 1 ms into a single event.
    idx = np.flatnonzero(spikes)
    if len(idx) == 0:
        return 0
    return int(1 + np.sum(np.diff(idx) > int(0.001 * sr)))


def bad_samples(x: np.ndarray) -> dict:
    x = np.asarray(x, np.float32)
    tiny = np.finfo(np.float32).tiny
    finite = np.isfinite(x)
    return {
        "nan": int(np.sum(np.isnan(x))),
        "inf": int(np.sum(np.isinf(x))),
        "denormal": int(np.sum(finite & (x != 0) & (np.abs(x) < tiny))),
    }
