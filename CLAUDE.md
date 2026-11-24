# CLAUDE.md — Metamorfo

Monophonic vocal pitch + formant manipulator (VST3 / AU / CLAP / Standalone), C++20 + JUCE 9.
Source of truth: [`docs/SPEC.md`](docs/SPEC.md). Progress and pending work: [`docs/ROADMAP.md`](docs/ROADMAP.md).

## How to work (owner's rules)
- One phase at a time (SPEC §10). Always start in **plan mode**; no code before the plan is approved.
- End of each phase: Release build + tests green, update this file and `docs/ROADMAP.md`,
  commit as `phase N: ...`, summary in Spanish with step-by-step FL Studio test.
- Code and comments in **English**. Messages to the owner and user docs in **Spanish**.
- Anything pending goes to `docs/ROADMAP.md`. No hidden TODOs.
- "Done" = builds on Windows + macOS (CI), unit tests pass, pluginval (strictness 10) passes.
- If something is unfeasible or there is a clearly better option, say so before implementing.

## Commands
```bash
cmake --preset release            # configure (Ninja, Release) -> build/release
cmake --build --preset release    # build all formats + tests
ctest --preset release            # unit tests (Linux headless: xvfb-run -a ctest ...)
```
- Windows: run from "Developer PowerShell for VS". `cmake --preset vs` generates a VS 2022 solution.
- Set `CPM_SOURCE_CACHE` (e.g. `~/.cache/CPM`) to avoid re-downloading dependencies.
- Artefacts: `build/release/Metamorfo_artefacts/Release/{VST3,CLAP,AU,Standalone}`.
- CI: `.github/workflows/build.yml` (windows-latest + macos-latest universal), validators via
  `tools/ci/validate_windows.ps1` and `tools/ci/validate_macos.sh`.
- DSP audit (needs numpy + scipy): build target `MetamorfoRender`, then
  `python tools/analysis/run_audit.py --render build/release/tests/MetamorfoRender --phase N`
  → `tests/render/output/` (audit_table.md + listening WAVs). ✘ = bug, ⏸ = later phase, ✔* = trivial pass.
  `MetamorfoRender` also counts heap allocations inside processBlock (must stay 0).
  CI runs it with `--fail-on-x`; bump `--phase` in build.yml when a phase is closed.
- Sanitizers (Linux/macOS): `cmake --preset asan|tsan`, build, `xvfb-run -a ctest --preset asan|tsan`
  (tsan: build target MetamorfoTests, run with `-E host_probe`). CI job `linux-sanitizers`.
- Fuzzer: `MetamorfoFuzz [--seed N] [--episodes N] [--blocks N] [--no-bypass]` (CTest `fuzz_short`;
  CI runs it under ASan). Invariants: finite bounded output, constant latency, 0 allocations.
- Quality matrix (numpy + scipy): `python tools/analysis/quality_matrix.py --render <MetamorfoRender>
  --out tests/render/output/quality --fail` (4 voices x shifts/formant/Link/Robot vs ideal voice; CI).
- Static analysis: `clang-tidy -p build/release src/**/*.cpp` (config `.clang-tidy`, must stay at 0).
- Soak (1 h of audio, hidden tag): `MetamorfoTests "[.soak]"`. Standalone smoke: CTest `standalone_smoke`.
- Installers: `installer/windows/Metamorfo.iss` (Inno Setup 6, built + install-tested in CI),
  `installer/macos/build_pkg.sh <artefacts> <version>` (pkgbuild, install-tested in CI).
- Host probe: `MetamorfoHostProbe vst3|clap <path>` loads the BUILT binaries like a DAW
  (CTest `host_probe_vst3` / `host_probe_clap`; checks MIDI input bus / CLAP MIDI note port).

## Unbreakable rules
- **Real time** (`processBlock` and everything it calls): no allocation, no locks, no I/O, no logging,
  no exceptions. Allocate in `prepareToPlay`. `juce::ScopedNoDenormals`. Output sanitised (no NaN/Inf).
- **No mutable static state** (FL runs many instances on several threads).
- Output sanitiser zeroes NaN, Inf **and denormals** (input denormals would pass through otherwise).
- Never compare parameter floats with platform-dependent residues: stepped params use `steppedRange`
  (exact decimal steps; arm64 regression B2).
- Never trust the host block size: process in chunks of at most the prepared size (FL can exceed it).
- Latency = 2 x ceil(sr/60) + 3 (drive oversampling) = 1603 @ 48 kHz; neutral settings must stay bit-exact (identity path,
  `out = dry + mix*(processed-dry)`); tests compare against the input delayed by the latency.
- Smoothed parameters (no zipper noise). Latency is fixed per sample rate and always reported
  with `setLatencySamples()` in `prepareToPlay`; the dry path is delayed by the same amount.
- UI reads audio state only through atomics; closing the editor never affects audio.
- Catch2 test names must be ASCII-only (CTest on Windows mangles non-ASCII filters).
- Host bypass = our `processBlockBypassed`: input delayed by the latency, 20 ms fade. Never rely on
  JUCE's default (no delay, no sanitising, mono->stereo silences R).
- Text typed by users goes through `params::parseNumber` (decimal comma, U+2212 minus, units).
  Preset names/categories go through `safeComponent` (one path component inside the presets folder).
- On/off parameters snap their value (`SnappingBoolParameter`); never `AudioParameterBool` (raw values
  break state round-trips).
- Tests that delete an editor must call `processor.editorBeingDeleted (editor)` first (JUCE 9 host
  protocol). Test threads must be joined via RAII (`StopAndJoin`), never after a REQUIRE.
- **Legal (SPEC §1):** no third-party product/brand names in code, UI, presets or user docs.
  Only permissive deps (MIT/BSD/Apache/BSL). Keep `THIRD_PARTY_LICENSES.md` current.

## Frozen identity (never change: hosts would see a different plugin, projects break)
| Field | Value |
|---|---|
| Company | `Metamorfo Audio` |
| Manufacturer code / plugin code | `Mtmf` / `Mmf1` |
| Bundle ID / CLAP ID | `com.metamorfoaudio.metamorfo` |
| AU type | `aumf` (kAudioUnitType_MusicEffect) |
| VST3 categories | `Fx`, `Pitch Shift` |

## Frozen parameter contract (`src/plugin/Parameters.*`)
Order and IDs are fixed (FL automation is stored by index): `pitch, formant, link, mode, drive, mix,
midiReference, portamento`, all with version hint 1. New parameters only appended, higher hint.
State = APVTS XML, root `MetamorfoState`, attribute `stateVersion` (currently 1); migrations go in
`setStateInformation`.

## Layout
```
cmake/            GetCPM.cmake (pinned CPM), Dependencies.cmake (ALL pinned deps), CompilerSettings.cmake
src/dsp/          JUCE-free RT DSP (static lib metamorfo_dsp, strict warnings): Biquad, PitchDetector
                  (decimated YIN + full-rate refinement), VoicingDetector, ModeController,
                  PsolaEngine (TD-PSOLA, formant = grain resampling, Link), TubeDrive (+HalfBand2x),
                  MidiPitchControl (note stack + portamento)
src/plugin/       PluginProcessor, Parameters, PresetManager (factory/user presets, A/B), ParameterLocks
src/ui/           PluginEditor (780x260 base canvas, scaled 75-200 %), MetamorfoLookAndFeel,
                  components/ Knob, ToggleLever3, PresetBar + MidiHelpScreen; fonts in resources/fonts (OFL)
tests/unit/       Catch2 tests (own copy of JUCE + core, custom main with ScopedJuceInitialiser_GUI),
                  incl. Bypass, Limits (+ [.soak]), UiInteraction (synthetic mouse), PresetRobustness
tests/fuzz/       MetamorfoFuzz: seeded chaos fuzzer of the processor
tests/render/     MetamorfoRender: offline host (WAV in/out, params, MIDI, JSON report, alloc probe)
tests/screenshot/ MetamorfoScreenshot <dir>: editor PNGs for visual review (docs/images)
tests/host/       MetamorfoHostProbe: VST3 (JUCE hosting) + minimal CLAP host for the built binaries
tools/analysis/   Python audit: signals.py (synthetic voice), metrics.py, run_audit.py,
                  quality.py + quality_matrix.py (voice-type quality matrix)
tools/ci/         validation scripts used by CI, standalone_smoke.sh, lsan.supp
installer/        Windows (Inno Setup) and macOS (pkg) installers
docs/             SPEC, ROADMAP, TESTING_FL, TEST_REPORT, DAW_MATRIX, GUIA_USUARIO_es, images/
```
- `metamorfo_core` is an INTERFACE library (sources + JUCE modules) linked by both the plugin and
  the tests, so tests run without any host.

## Key decisions (details in ROADMAP "Decisiones")
- JUCE **9.0.6** (owner approved, replaces the SPEC's JUCE 8). CLAP via `free-audio/clap-juce-extensions`
  pinned at `3837f4a`; JUCE 9 has no native CLAP yet.
- Dependencies via CPM (git, pinned tag/commit). Fallback if CLAP ext breaks: JUCE 9.0.0.
- Static MSVC runtime (`/MT`). macOS: universal arm64+x86_64, deployment target 12.0, ad-hoc signing.
- Buses: mono→mono, mono→stereo (duplicated), stereo→stereo. Stereo→mono rejected.
- MIDI: `NEEDS_MIDI_INPUT`, not a synth, not a MIDI effect. CLAP wrapper exposes a note input port
  because `acceptsMidi()` is true. `MidiPitchControl` = last-note-priority stack, fixed storage.
- License: **AGPLv3** (`LICENSE.md`).

## Validation status (2026-11-24, full debug pass, see docs/TEST_REPORT.md §12)
CTest 95/95 · fuzz 105k blocks 0 failures · DSP audit --phase 7: 77/77 · quality matrix 101/101 ·
pluginval 10 (VST3, 4 seeds x randomise x 44.1-192 kHz x blocks 16-4096) + AU pass · Steinberg -e 537/537 ·
clap-validator 18/0/3 · auval pass · ASan/UBSan/TSan clean · clang-tidy 0 · 1 h soak bit-exact ·
CPU ~1.3 % of a core @48k/128, 0 allocs.
