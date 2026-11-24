#!/usr/bin/env python3
"""Metamorfo DSP audit (SPEC §7): renders synthetic signals through the plugin with
MetamorfoRender and measures pitch, formant, harmonics, latency, clicks, robustness,
allocations and CPU.

Usage:
  python tools/analysis/run_audit.py --render build/release/tests/MetamorfoRender \
      --out tests/render/output --phase 1 [--fail-on-x]

Each result is ✔ (passes), ✔* (passes only because the feature does not exist yet),
✘ (fails although the feature should already work) or ⏸ (fails because the feature
belongs to a later phase than --phase).
Writes <out>/audit_results.json and <out>/audit_table.md, plus listening renders.
"""
from __future__ import annotations

import argparse
import json
import math
import subprocess
import sys
from pathlib import Path

import numpy as np
from scipy.io import wavfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
import metrics as m  # noqa: E402
import signals as s  # noqa: E402

# Phase in which each feature is delivered (SPEC §10).
FEATURE_PHASE = {
    "detector": 2, "transpose": 3, "formant": 3, "link": 3, "mix": 3, "latency": 3,
    "quantize": 4, "robot": 4, "midi": 4, "drive": 5, "presets": 6,
    "robustness": 1, "realtime": 1, "cpu": 1, "clicks": 1, "harness": 1,
}

MODE = {"transpose": 0, "quantize": 1, "robot": 2}


class Audit:
    def __init__(self, render: Path, out: Path, phase: int):
        self.render_bin = render
        self.out = out
        self.work = out / "_work"
        self.work.mkdir(parents=True, exist_ok=True)
        self.phase = phase
        self.rows: list[dict] = []
        self.reports: list[dict] = []
        self.counter = 0

    # ------------------------------------------------------------------ plumbing
    def render(self, x: np.ndarray, sr: int, params: dict | None = None, notes=(), layout="stereo",
               block=512, irregular=False, param_at=(), repeat=1, keep_as: str | None = None):
        self.counter += 1
        tag = f"r{self.counter:04d}"
        src = self.work / f"{tag}_in.wav"
        dst = (self.out / keep_as) if keep_as else self.work / f"{tag}_out.wav"
        rep = self.work / f"{tag}.json"
        data = x if layout != "stereo" else np.stack([x, x], axis=1)
        wavfile.write(src, sr, data.astype(np.float32))
        cmd = [str(self.render_bin), "--in", str(src), "--out", str(dst), "--report", str(rep),
               "--layout", layout, "--repeat", str(repeat)]
        cmd += ["--blocks", "irregular"] if irregular else ["--block", str(block)]
        for k, v in (params or {}).items():
            cmd += ["--param", f"{k}={v}"]
        for t, k, v in param_at:
            cmd += ["--param-at", f"{t}:{k}={v}"]
        for start, dur, note, vel in notes:
            cmd += ["--note", f"{start}:{dur}:{note}:{vel}"]
        subprocess.run(cmd, check=True, capture_output=True)
        _, y = wavfile.read(dst)
        report = json.loads(rep.read_text())
        self.reports.append(report)
        return y.astype(np.float32), report

    def add(self, group, test, measured, expected, passed, feature, note=""):
        future = FEATURE_PHASE[feature] > self.phase
        if passed and future:
            # Passes only because the feature does not exist yet: not evidence of correctness.
            status = "✔*"
            note = (note + " " if note else "") + f"(trivial: la función llega en la Fase {FEATURE_PHASE[feature]})"
        elif passed:
            status = "✔"
        elif future:
            status = "⏸"
            note = (note + " " if note else "") + f"(se implementa en la Fase {FEATURE_PHASE[feature]})"
        else:
            status = "✘"
        self.rows.append(dict(group=group, test=test, measured=measured, expected=expected,
                              status=status, note=note))
        print(f"{status} {group} | {test} | {measured} | {expected} {note}", flush=True)

    # ------------------------------------------------------------------ tests
    def detector(self):
        sr = 48000
        for f in (80, 150, 300, 600, 1000):
            x = s.sine(f, 2.0, sr)
            ref = m.yin_f0(x, sr)
            err = m.cents(ref, f)
            self.add("Detector", f"Analizador de referencia (YIN numpy) · seno {f} Hz", f"{err:+.2f} ¢",
                     "|error| < 5 ¢", abs(err) < 5, "harness",
                     "valida la herramienta de medición, no el plugin")
        for f in (80, 150, 300, 600, 1000):
            _, rep = self.render(s.sine(f, 1.5, sr), sr, {})
            med = rep.get("detected_f0_median", 0.0)
            err = m.cents(med, f) if med > 0 else float("nan")
            self.add("Detector", f"Detector del plugin · seno {f} Hz",
                     f"{med:.2f} Hz ({err:+.2f} ¢), sonoro {100 * rep.get('voiced_ratio', 0):.0f} %" if med > 0
                     else "sin estimación", "|error| < 5 ¢", med > 0 and abs(err) < 5, "detector")
        for note in (45, 57, 69):
            f = s.midi_to_hz(note)
            _, rep = self.render(s.voice_note(f, 1.5, sr), sr, {})
            med = rep.get("detected_f0_median", 0.0)
            err = m.cents(med, f) if med > 0 else float("nan")
            self.add("Detector", f"Detector del plugin · voz sintética {f:.1f} Hz",
                     f"{err:+.2f} ¢" if med > 0 else "sin estimación", "|error| < 5 ¢, sin errores de octava",
                     med > 0 and abs(err) < 5, "detector")
        _, rep = self.render(s.white_noise(1.5, sr), sr, {})
        self.add("Detector", "Ruido blanco → no sonoro", f"sonoro {100 * rep.get('voiced_ratio', 0):.0f} %",
                 "< 5 %", rep.get("voiced_ratio", 1) < 0.05, "detector")

    def transpose(self):
        sr = 48000
        base = 220.0
        voice = s.voice_note(base, 2.0, sr)
        for st in (12, 7, 1, -1, -7, -12):
            y, _ = self.render(voice, sr, {"mode": MODE["transpose"], "pitch": st})
            f = m.yin_f0(y, sr)
            expected = base * 2 ** (st / 12)
            err = m.cents(f, expected)
            self.add("Transponer", f"Voz 220 Hz, PITCH {st:+d} st", f"{f:.2f} Hz ({err:+.1f} ¢)",
                     f"{expected:.2f} Hz ±5 ¢", abs(err) < 5, "transpose")
        x = s.sine(220.0, 2.0, sr)
        y, _ = self.render(x, sr, {"mode": MODE["transpose"], "pitch": 7})
        f = m.sine_freq(y, sr)
        expected = 220.0 * 2 ** (7 / 12)
        err = m.cents(f, expected)
        self.add("Transponer", "Seno 220 Hz, PITCH +7 st (SPEC §7)", f"{f:.2f} Hz ({err:+.1f} ¢)",
                 f"{expected:.2f} Hz ±5 ¢", abs(err) < 5, "transpose")

    def formant(self):
        sr = 48000
        voice = s.voice_note(220.0, 2.0, sr)
        c_in = m.spectral_centroid(voice, sr)
        for fm in (6, -6):
            y, _ = self.render(voice, sr, {"pitch": 0, "formant": fm})
            err = m.cents(m.yin_f0(y, sr), 220.0)
            c = m.spectral_centroid(y, sr)
            change = 100 * (c / c_in - 1)
            moved = change > 3 if fm > 0 else change < -3
            self.add("Formante", f"FORMANT {fm:+d}, PITCH 0: f0 estable", f"{err:+.1f} ¢", "|Δf0| < 5 ¢",
                     abs(err) < 5, "formant")
            self.add("Formante", f"FORMANT {fm:+d}, PITCH 0: centroide", f"{c:.0f} Hz ({change:+.1f} %)",
                     f"{'sube' if fm > 0 else 'baja'} > 3 % (entrada {c_in:.0f} Hz)", moved, "formant")

    def link(self):
        sr = 48000
        voice = s.voice_note(220.0, 2.0, sr)
        c_in = m.spectral_centroid(voice, sr)
        expected_f = 220.0 * 2 ** (5 / 12)
        res = {}
        for link in (0, 1):
            y, _ = self.render(voice, sr, {"pitch": 5, "formant": 0, "link": link})
            res[link] = (m.cents(m.yin_f0(y, sr), expected_f), m.spectral_centroid(y, sr) / c_in)
        ok_f = abs(res[0][0]) < 5 and abs(res[1][0]) < 5
        ok_c = abs(res[0][1] - 1) < 0.05 and res[1][1] > 1.15
        self.add("Link", "PITCH +5: f0 con Link OFF / ON", f"{res[0][0]:+.1f} ¢ / {res[1][0]:+.1f} ¢",
                 "ambos ±5 ¢ de +5 st", ok_f, "link")
        self.add("Link", "PITCH +5: centroide relativo OFF / ON", f"×{res[0][1]:.3f} / ×{res[1][1]:.3f}",
                 "OFF ≈ ×1.00 (±5 %), ON > ×1.15 (≈ ×1.33)", ok_c, "link")

    def quantize(self):
        sr = 48000
        for detune in (35, -40):
            x = s.voice_note(s.midi_to_hz(57) * 2 ** (detune / 1200), 2.0, sr)
            y, _ = self.render(x, sr, {"mode": MODE["quantize"], "pitch": 0})
            err = m.cents(m.yin_f0(y, sr), s.midi_to_hz(57))
            self.add("Cuantizar", f"Voz A3 {detune:+d} ¢", f"{err:+.1f} ¢ de A3", "|error| < 5 ¢",
                     abs(err) < 5, "quantize")

    def robot(self):
        sr = 48000
        voice = s.voice_note(220.0, 2.0, sr)
        y, _ = self.render(voice, sr, {"mode": MODE["robot"], "pitch": 0})
        err = m.cents(m.yin_f0(y, sr), s.midi_to_hz(72))
        self.add("Robot", "PITCH 0, sin MIDI → MIDI 72 (523.25 Hz)", f"{err:+.1f} ¢", "|error| < 5 ¢",
                 abs(err) < 5, "robot")
        y, _ = self.render(voice, sr, {"mode": MODE["robot"], "pitch": 3})
        err = m.cents(m.yin_f0(y, sr), s.midi_to_hz(75))
        self.add("Robot", "PITCH +3, sin MIDI → MIDI 75", f"{err:+.1f} ¢", "|error| < 5 ¢", abs(err) < 5, "robot")
        for note in (64, 67, 48):
            y, rep = self.render(voice, sr, {"mode": MODE["robot"], "pitch": 0}, notes=[(0.0, 2.0, note, 100)])
            err = m.cents(m.yin_f0(y, sr), s.midi_to_hz(note))
            self.add("Robot", f"Nota MIDI {note} sostenida", f"{err:+.1f} ¢", "|error| < 5 ¢",
                     abs(err) < 5, "midi")

    def drive(self):
        sr = 48000
        x = s.sine(220.0, 2.0, sr, amp=0.25)
        y0, _ = self.render(x, sr, {"drive": 0})
        self.add("Drive", "DRIVE 0 transparente (null vs entrada)", f"{m.null_db(x, y0):.1f} dBFS",
                 "< −90 dBFS", m.null_db(x, y0) < -90, "drive")
        base_rms = m.rms_db(m.steady(y0[:, 0], sr))
        for d in (6, 12):
            y, _ = self.render(x, sr, {"drive": d})
            h = m.harmonic_levels_db(y, sr, 220.0)
            delta = m.rms_db(m.steady(y[:, 0], sr)) - base_rms
            h2_rel, h3_rel = h[1] - h[0], h[2] - h[0]
            self.add("Drive", f"DRIVE {d}: H2 vs H3 (rel. H1)", f"H2 {h2_rel:.1f} dB, H3 {h3_rel:.1f} dB",
                     "H2 > H3 y H2 > −60 dB", (h2_rel > h3_rel) and h2_rel > -60, "drive")
            self.add("Drive", f"DRIVE {d}: cambio de nivel RMS", f"{delta:+.2f} dB", "±1.5 dB",
                     abs(delta) <= 1.5, "drive", "compensación automática")

    def mix_and_latency(self):
        for sr in (44100, 48000, 96000):
            phrase = s.voice_phrase(sr)
            y, rep = self.render(phrase, sr, {"mix": 0, "pitch": 7})
            reported = rep["reported_latency_samples"]
            nd = m.null_db(phrase, y, reported)
            if sr == 48000:
                self.add("Mix", "MIX 0 % (PITCH +7) null vs entrada, alineado por latencia",
                         f"{nd:.1f} dBFS" if math.isfinite(nd) else "−∞ (null perfecto)",
                         "null perfecto (−∞ / < −120 dBFS)", nd < -120, "mix")
            imp = s.impulse(1.0, sr)
            y_dry, rep_dry = self.render(imp, sr, {"mix": 0})
            y_wet, rep_wet = self.render(phrase, sr, {"mix": 100, "pitch": 0})
            lag_dry = m.latency_samples(imp, y_dry)
            lag_wet = m.latency_samples(phrase, y_wet)
            ok = lag_dry == reported and abs(lag_wet - reported) <= 1
            self.add("Latencia", f"{sr / 1000:g} kHz: medida (dry / wet) vs reportada",
                     f"{lag_dry} / {lag_wet} vs {reported} muestras ({1000 * reported / sr:.1f} ms)",
                     "iguales (±1 muestra), fija, ≤ 30 ms (máx. 60)", ok, "latency",
                     "en Fase 1 el motor no existe: latencia 0" if reported == 0 else "")

    def clicks(self):
        sr = 48000
        phrase = s.voice_phrase(sr)
        c_in = m.clicks(phrase, sr)
        y, _ = self.render(phrase, sr, {"pitch": 0})
        self.add("Clicks", "Transiciones voz/silencio (PITCH 0)", f"{m.clicks(y, sr)} (entrada {c_in})",
                 "≤ clicks de la entrada", m.clicks(y, sr) <= c_in, "clicks")
        y, _ = self.render(phrase, sr, {"pitch": 5})
        self.add("Clicks", "Transiciones voz/silencio (PITCH +5)", f"{m.clicks(y, sr)} (entrada {c_in})",
                 "≤ clicks de la entrada", m.clicks(y, sr) <= c_in, "transpose")
        changes = [(0.4, "mode", 1), (0.9, "mode", 2), (1.5, "mode", 0), (2.2, "mode", 2), (2.8, "mode", 1)]
        y, _ = self.render(phrase, sr, {"pitch": 3}, param_at=changes)
        self.add("Clicks", "Cambios de modo durante la reproducción (5 cambios)",
                 f"{m.clicks(y, sr)} (entrada {c_in})", "≤ clicks de la entrada", m.clicks(y, sr) <= c_in, "robot")
        jumps = [(0.5, "pitch", 12), (1.0, "pitch", -12), (1.6, "formant", 8), (2.1, "drive", 12), (2.6, "mix", 30)]
        y, _ = self.render(phrase, sr, {}, param_at=jumps)
        self.add("Clicks", "Saltos bruscos de parámetros (zipper)", f"{m.clicks(y, sr)} (entrada {c_in})",
                 "≤ clicks de la entrada", m.clicks(y, sr) <= c_in, "transpose", "requiere parámetros suavizados")

    def robustness(self):
        sr = 48000
        inputs = {
            "silencio": s.silence(1.5, sr),
            "ruido blanco": s.white_noise(1.5, sr),
            "seno 0 dBFS": s.full_scale(1.5, sr),
            "ruido denormal": s.denormal_noise(1.5, sr),
        }
        blocks = [("16", dict(block=16)), ("64", dict(block=64)), ("512", dict(block=512)),
                  ("4096", dict(block=4096)), ("irregular 1–4096", dict(irregular=True))]
        for name, x in inputs.items():
            for bname, kw in blocks:
                worst = {"nan": 0, "inf": 0, "denormal": 0}
                identical = True
                for layout in ("mono", "stereo", "mono-stereo"):
                    y, rep = self.render(x, sr, {}, layout=layout, **kw)
                    for k, v in m.bad_samples(y).items():
                        worst[k] = max(worst[k], v)
                    yy = y if y.ndim == 1 else y[:, 0]
                    identical &= bool(np.array_equal(yy, x))
                ok = worst == {"nan": 0, "inf": 0, "denormal": 0}
                note = "salida idéntica a la entrada (passthrough)" if identical else ""
                self.add("Robustez", f"{name} · buffer {bname} · mono/estéreo/mono→estéreo",
                         f"NaN {worst['nan']}, Inf {worst['inf']}, denormales {worst['denormal']}",
                         "0 / 0 / 0", ok, "robustness", note)
        # Extreme settings only exercise real DSP once phases 3-5 exist.
        extreme = {"pitch": 12, "formant": -12, "link": 1, "mode": 2, "drive": 12, "mix": 50, "portamento": 200}
        for name in ("ruido blanco", "seno 0 dBFS"):
            y, _ = self.render(inputs[name], sr, extreme, irregular=True)
            b = m.bad_samples(y)
            self.add("Robustez", f"{name} · parámetros extremos · buffers irregulares",
                     f"NaN {b['nan']}, Inf {b['inf']}, denormales {b['denormal']}, pico {np.max(np.abs(y)):.3f}",
                     "0 / 0 / 0, sin explosión", sum(b.values()) == 0 and np.max(np.abs(y)) < 4, "drive")
        for rate in (44100, 96000, 192000):
            x = s.white_noise(1.0, rate)
            y, _ = self.render(x, rate, {}, block=4096)
            b = m.bad_samples(y)
            self.add("Robustez", f"Sample rate {rate / 1000:g} kHz", f"NaN {b['nan']}, Inf {b['inf']}",
                     "0 / 0", sum(b.values()) == 0, "robustness")

    def realtime(self):
        worst = max(r["allocations_in_process"] for r in self.reports)
        total = max(r["allocations_total"] for r in self.reports)
        self.add("Tiempo real", f"Alocaciones dentro de processBlock ({len(self.reports)} renders)",
                 f"{worst} (sonda activa: {total} alocaciones fuera)", "0", worst == 0 and total > 0, "realtime")

    def cpu(self):
        sr = 48000
        phrase = s.voice_phrase(sr)
        _, rep = self.render(phrase, sr, {"pitch": 7}, block=128, repeat=40)
        pct = rep["cpu_percent_of_core"]
        self.add("CPU", f"1 instancia, 48 kHz, buffer 128 ({rep['audio_seconds']:.0f} s de audio)",
                 f"{pct:.3f} % de un núcleo", "< 2 %", pct < 2.0, "cpu",
                 "contenedor Linux; sin motor DSP todavía (se re-mide en Fases 2–5)")
        _, rep = self.render(phrase, sr, {"pitch": 7}, block=16, repeat=20)
        self.add("CPU", "1 instancia, 48 kHz, buffer 16 (peor caso de overhead)",
                 f"{rep['cpu_percent_of_core']:.3f} % de un núcleo", "< 2 %", rep["cpu_percent_of_core"] < 2.0, "cpu")

    def listening_renders(self):
        sr = 48000
        phrase = s.voice_phrase(sr)
        wavfile.write(self.out / "00_entrada_voz_sintetica.wav", sr, phrase)
        jobs = [
            ("01_transponer_+7.wav", {"mode": 0, "pitch": 7}, ()),
            ("02_transponer_-12.wav", {"mode": 0, "pitch": -12}, ()),
            ("03_transponer_+12.wav", {"mode": 0, "pitch": 12}, ()),
            ("04_formante_-3_coros.wav", {"mode": 0, "formant": -3}, ()),
            ("05_link_on_+5.wav", {"mode": 0, "pitch": 5, "link": 1}, ()),
            ("06_cuantizar.wav", {"mode": 1, "pitch": 0}, ()),
            ("07_robot_pitch0.wav", {"mode": 2, "pitch": 0}, ()),
            ("08_robot_melodia_midi.wav", {"mode": 2},
             ((0.0, 0.7, 60, 100), (0.7, 0.5, 64, 100), (1.2, 0.6, 67, 100), (1.8, 1.0, 72, 100))),
            ("09_drive_8.wav", {"drive": 8}, ()),
            ("10_mix_50_+7.wav", {"mix": 50, "pitch": 7}, ()),
        ]
        for name, params, notes in jobs:
            self.render(phrase, sr, params, notes=notes, keep_as=name)
        self.add("Renders", f"{len(jobs) + 1} WAV de escucha en tests/render/output/", f"{len(jobs) + 1} archivos",
                 "uno por modo + variantes", True, "harness", "en Fase 1 todos suenan igual que la entrada")
        self.add("Renders", "Un WAV por preset de fábrica (16)", "no existen presets", "16 archivos",
                 False, "presets")

    # ------------------------------------------------------------------ output
    def write(self):
        (self.out / "audit_results.json").write_text(json.dumps(self.rows, ensure_ascii=False, indent=1))
        lines = ["| # | Grupo | Prueba | Medido | Esperado | Resultado | Nota |", "|---|---|---|---|---|---|---|"]
        def cell(v):  # pipes inside values would break the markdown table
            return str(v).replace("|", "\\|")

        for i, r in enumerate(self.rows, 1):
            lines.append(f"| {i} | {cell(r['group'])} | {cell(r['test'])} | {cell(r['measured'])} | "
                         f"{cell(r['expected'])} | {r['status']} | {cell(r['note'])} |")
        (self.out / "audit_table.md").write_text("\n".join(lines) + "\n")
        counts = {k: sum(r["status"] == k for r in self.rows) for k in ("✔", "✔*", "✘", "⏸")}
        print(f"\nTotal: {len(self.rows)} · ✔ {counts['✔']} · ✔* {counts['✔*']} · ✘ {counts['✘']} · ⏸ {counts['⏸']}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--render", required=True, type=Path)
    ap.add_argument("--out", default=Path("tests/render/output"), type=Path)
    ap.add_argument("--phase", default=1, type=int)
    ap.add_argument("--fail-on-x", action="store_true",
                    help="exit with code 1 if any check is ✘ (⏸ and ✔* never fail)")
    args = ap.parse_args()

    audit = Audit(args.render.resolve(), args.out.resolve(), args.phase)
    for step in (audit.detector, audit.transpose, audit.formant, audit.link, audit.quantize, audit.robot,
                 audit.drive, audit.mix_and_latency, audit.clicks, audit.robustness, audit.listening_renders,
                 audit.cpu, audit.realtime):
        step()
    audit.write()

    failed = [r for r in audit.rows if r["status"] == "✘"]
    if args.fail_on_x and failed:
        print(f"FAIL: {len(failed)} check(s) marked ✘", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
