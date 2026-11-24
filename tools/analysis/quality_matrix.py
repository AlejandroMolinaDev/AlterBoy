#!/usr/bin/env python3
"""Quality matrix: every voice type x pitch shift / formant / Link / Robot through the
real processor (MetamorfoRender), compared with the ideal re-synthesised voice.

Usage:
  python tools/analysis/quality_matrix.py --render build/release/tests/MetamorfoRender \
      --out tests/render/output/quality [--fail]

Writes quality.md / quality.json. With --fail, exits 1 if any case exceeds the
thresholds below (regression guard for CI).
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

import numpy as np
from scipy.io import wavfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
import metrics as m  # noqa: E402
import quality as q  # noqa: E402
import signals as s  # noqa: E402

SR = 48000
SECONDS = 2.0

# Regression thresholds, calibrated on the full matrix (docs/TEST_REPORT.md §12).
# The SPEC's ±5 cents acceptance tests live in Catch2 (sine +7, voice ±1/±7/±12,
# quantize, robot) and pass; this matrix guards against regressions over all voices.
MAX_CENTS = 6.0             # f0 error on steady voices (worst measured: Link -12 at 450 Hz, +5.4)
MAX_HNR_DROP_DB = 12.0      # harmonic-to-noise ratio lost vs the ideal voice
HNR_CAP_DB = 45.0           # the ideal's HNR saturates near 50 dB; above 45 dB noise is inaudible


def max_harmonic_db(f0: float, pitch_st: float = 0.0, formant_st: float = 0.0) -> float:
    """Harmonic-amplitude distance limit. TD-PSOLA smooths the spectral envelope over ±f0,
    so for high voices (f0 >= 400 Hz, formant bandwidths << harmonic spacing in the
    reference) the distance to the ideal is larger: a known method limit (ROADMAP).
    Same for formant raised far above the pitch (formant - pitch >= 19 st, e.g. -12 st with
    formant +12): the resampled grains then cover only ~1/4 of the synthesis period."""
    limit = 6.5 if f0 < 400.0 else 9.5
    if formant_st - pitch_st >= 19.0:
        limit = 11.0
    return limit


def render(binary: Path, work: Path, x: np.ndarray, params: dict, notes=(), tag="q") -> np.ndarray:
    src, dst = work / f"{tag}_in.wav", work / f"{tag}_out.wav"
    wavfile.write(src, SR, np.stack([x, x], 1).astype(np.float32))
    cmd = [str(binary), "--in", str(src), "--out", str(dst), "--report", str(work / f"{tag}.json"), "--block", "256"]
    for k, v in params.items():
        cmd += ["--param", f"{k}={v}"]
    for n in notes:
        cmd += ["--note", f"{n[0]}:{n[1]}:{n[2]}:{n[3]}"]
    subprocess.run(cmd, check=True, capture_output=True)
    _, y = wavfile.read(dst)
    rep = json.loads((work / f"{tag}.json").read_text())
    lat = rep["reported_latency_samples"]
    return y[lat:, 0].astype(np.float32)


def case(binary, work, voice_name, f0, params, target_f0, formant_scale, label, notes=()):
    x = s.voice_note(f0, SECONDS, SR)
    y = render(binary, work, x, params, notes)
    ideal = s.voice_note(target_f0, SECONDS, SR, formant_scale=formant_scale)
    measured = m.yin_f0(y, SR)
    cents = m.cents(measured, target_f0) if measured == measured else float("nan")
    lsd = q.envelope_lsd_db(y, ideal, SR)
    had = q.harmonic_distance_db(y, ideal, SR, target_f0)
    hnr_out, hnr_ideal = q.hnr_db(y, SR, target_f0), q.hnr_db(ideal, SR, target_f0)
    limit = max_harmonic_db(f0, params.get("pitch", 0.0), params.get("formant", 0.0) if not params.get("link") else params.get("pitch", 0.0))
    ok = abs(cents) < MAX_CENTS and had < limit and (min(hnr_ideal, HNR_CAP_DB) - hnr_out) < MAX_HNR_DROP_DB
    row = dict(voice=voice_name, f0=f0, case=label, cents=round(cents, 2), lsd_db=round(lsd, 2), harm_db=round(had, 2),
               hnr_out=round(hnr_out, 1), hnr_ideal=round(hnr_ideal, 1), ok=bool(ok))
    print(("OK  " if ok else "BAD ") + json.dumps(row, ensure_ascii=False), flush=True)
    return row


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--render", required=True, type=Path)
    ap.add_argument("--out", default=Path("tests/render/output/quality"), type=Path)
    ap.add_argument("--fail", action="store_true")
    args = ap.parse_args()
    out = args.out.resolve()
    work = out / "_work"
    work.mkdir(parents=True, exist_ok=True)
    binary = args.render.resolve()

    rows = []
    for name, f0 in s.VOICES:
        for st in (-12, -7, -3, -1, 1, 3, 7, 12):
            target = f0 * 2 ** (st / 12)
            if not 60 <= target <= 1200:
                continue
            rows.append(case(binary, work, name, f0, {"pitch": st}, target, 1.0, f"Transponer {st:+d}"))
        for fm in (-12, -6, -3, -1, 1, 3, 6, 12):
            rows.append(case(binary, work, name, f0, {"formant": fm}, f0, 2 ** (fm / 12), f"Formante {fm:+d}"))
        for st in (-12, 12):
            target = f0 * 2 ** (st / 12)
            if 60 <= target <= 1200:
                rows.append(case(binary, work, name, f0, {"pitch": st, "link": 1}, target, 2 ** (st / 12), f"Link {st:+d}"))
        for st, fm in ((4, 6), (7, 12), (12, 12), (-7, -6), (12, -12), (-12, 12), (5, -5)):
            target = f0 * 2 ** (st / 12)
            if 60 <= target <= 1200:
                rows.append(case(binary, work, name, f0, {"pitch": st, "formant": fm}, target, 2 ** (fm / 12),
                                 f"Transponer {st:+d} + formante {fm:+d}"))
        rows.append(case(binary, work, name, f0, {"mode": 2}, s.midi_to_hz(72), 1.0, "Robot (MIDI 72)"))

    (out / "quality.json").write_text(json.dumps(rows, ensure_ascii=False, indent=1))
    lines = ["| Voz | f0 | Caso | Error f0 (¢) | Distancia armónica (dB) | LSD envolvente (dB, informativo) | HNR salida / ideal (dB) | OK |",
             "|---|---|---|---|---|---|---|---|"]
    for r in rows:
        lines.append(f"| {r['voice']} | {r['f0']:.0f} | {r['case']} | {r['cents']} | {r['harm_db']} | {r['lsd_db']} | "
                     f"{r['hnr_out']} / {r['hnr_ideal']} | {'✔' if r['ok'] else '✘'} |")
    (out / "quality.md").write_text("\n".join(lines) + "\n")
    bad = [r for r in rows if not r["ok"]]
    print(f"\n{len(rows)} cases, {len(bad)} outside thresholds")
    if args.fail and bad:
        sys.exit(1)


if __name__ == "__main__":
    main()
