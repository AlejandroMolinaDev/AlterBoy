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

## Unbreakable rules
- **Real time** (`processBlock` and everything it calls): no allocation, no locks, no I/O, no logging,
  no exceptions. Allocate in `prepareToPlay`. `juce::ScopedNoDenormals`. Output sanitised (no NaN/Inf).
- **No mutable static state** (FL runs many instances on several threads).
- Never trust the host block size: process in chunks of at most the prepared size (FL can exceed it).
- Smoothed parameters (no zipper noise). Latency is fixed per sample rate and always reported
  with `setLatencySamples()` in `prepareToPlay`; the dry path is delayed by the same amount.
- UI reads audio state only through atomics; closing the editor never affects audio.
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
src/dsp/          JUCE-free RT DSP (static lib metamorfo_dsp, strict warnings)
src/plugin/       PluginProcessor, Parameters
src/ui/           PluginEditor
tests/unit/       Catch2 tests (own copy of JUCE + core, custom main with ScopedJuceInitialiser_GUI)
tools/ci/         validation scripts used by CI
docs/             SPEC, ROADMAP, TESTING_FL
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

## Validation status (phase 1, Linux local check; CI is authoritative)
pluginval strictness 10: pass · Steinberg validator v3.8.0: 47/47 · clap-validator 0.3.2: 18 pass,
3 skipped (no preset-discovery factory, N/A) · auval: CI only.
