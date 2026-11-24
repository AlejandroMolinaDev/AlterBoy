# Reporte de auditoría de pruebas — Metamorfo

- **Fecha:** 2026-11-23
- **Commit auditado:** `0ffbf8f` (Fase 1 + corrección de los bugs B1 y B2)
- **Alcance:** todo el SPEC §7, aplicado al estado actual del proyecto.

> **Contexto importante.** Solo está implementada la **Fase 1**: un passthrough con entrada MIDI, 8 parámetros congelados que todavía no hacen nada y una UI de diagnóstico. El detector, PSOLA, el formante, Link, Cuantizar, Robot, Drive, Mix, la latencia del motor y los presets **no existen todavía**. Las pruebas de esas funciones ya están escritas y se ejecutan, pero su resultado se marca así:
>
> - **✔**: pasa y la función ya existe.
> - **✔\***: pasa **solo porque la función todavía no existe** (por ejemplo, "Drive no cambia el nivel" porque Drive no hace nada). No prueba nada; se vuelve a medir en su fase.
> - **⏸**: falla porque la función corresponde a una fase posterior. No es un bug.
> - **✘**: falla aunque la función ya debería funcionar. **Es un bug.**

## Resumen

| Área | Resultado |
|---|---|
| Compilación Release desde cero (Linux, GCC 13) | ✔ 0 warnings en código propio · 1 warning en JUCE · 3 avisos informativos de LTO · 5 min 07 s |
| Compilación en CI: Windows x64 (MSVC) | ✔ 0 warnings propios · 2 avisos por `JUCE_DISPLAY_SPLASH_SCREEN` (B5) |
| Compilación en CI: macOS Universal (clang) | ✔ 0 warnings propios · 544 `-Wunused-parameter` en clap-helpers, código de terceros (B6) |
| Tests Catch2 (16 casos, 64 088 aserciones) | ✔ 16/16 en Linux, Windows y macOS |
| pluginval nivel 10, VST3 (Linux) | ✔ SUCCESS, 25 grupos de pruebas |
| pluginval nivel 10, VST3 y AU (macOS, CI) | ✔ SUCCESS en ambos |
| pluginval nivel 10, VST3 (Windows, CI) | ✘ **no se pudo evaluar** por un error del script del CI (B3) |
| pluginval sobre CLAP | N/A: pluginval 1.0.4 no soporta CLAP ("No types found") |
| Validador VST3 de Steinberg 3.8.0 | ✔ 47/47 (Linux y macOS) |
| clap-validator 0.3.2 | ✔ 18 pasan · 0 fallan · 3 omitidas (preset-discovery, N/A) (Linux y macOS) |
| auval `aumf Mmf1 Mtmf` (macOS, CI) | ✔ AU VALIDATION SUCCEEDED |
| Binario Universal (`lipo`) | ✔ x86_64 + arm64 en VST3, AU, CLAP y Standalone |
| Harness DSP (73 pruebas) | Diagnóstico: ✔ 28 · ✔\* 14 · ✘ 5 · ⏸ 26 → **tras corregir: ✔ 33 · ✔\* 14 · ✘ 0 · ⏸ 26** |
| Alocaciones dentro de `processBlock` (111 renders) | ✔ 0 |
| CPU: 1 instancia, 48 kHz, buffer 128 | ✔ 0.006 % de un núcleo (sin motor DSP; se vuelve a medir en las Fases 2 a 5) |

## Herramientas nuevas de la auditoría

| Herramienta | Qué hace |
|---|---|
| `tests/render/RenderMain.cpp` → `MetamorfoRender` | Host offline. Procesa WAV con parámetros, cambios de parámetros durante la reproducción, notas MIDI simuladas, buffers fijos o irregulares, layouts mono y estéreo, y modo benchmark. Escribe un JSON con la latencia reportada, NaN/Inf/denormales, CPU y **alocaciones dentro de `processBlock`** (cuenta las llamadas a `operator new`). |
| `tools/analysis/signals.py` | Genera senos, senos desafinados, ruido, señal a 0 dBFS, ruido denormal, impulsos y **voz sintética** (pulso glótico de Rosenberg + 4 formantes de la vocal /a/, con vibrato y jitter). |
| `tools/analysis/metrics.py` | Mide f0 por FFT de alta resolución y por YIN, centroide espectral, armónicos H1 a H5, RMS, latencia por correlación cruzada, null test, clicks por segunda diferencia y NaN/Inf/denormales. |
| `tools/analysis/run_audit.py` | Corre toda la auditoría y escribe `tests/render/output/audit_results.json`, `audit_table.md` y los WAV de escucha. El parámetro `--phase N` decide qué fallos son ⏸. |

Para reproducirla, primero compila el host de render:

```bash
cmake --build --preset release --target MetamorfoRender
```

Luego instala las dependencias de Python:

```bash
pip install numpy scipy
```

Y corre la auditoría:

```bash
python tools/analysis/run_audit.py --render build/release/tests/MetamorfoRender --out tests/render/output --phase 1
```

## 1. Compilación

- **Build limpio en Linux:** se borró `build/` y se compiló en Release con todos los formatos, los tests y el host de render. Terminó en 5 min 07 s, con LTO. No hubo errores. Warnings:
  - Código propio (`src/`, `tests/`): **0**.
  - JUCE: 1, `-Wdangling-reference` en `juce_LookAndFeel_V2.cpp:413`. Es un falso positivo conocido de GCC 13 y no aparece en MSVC ni en clang.
  - LTO: 3 avisos informativos ("serial compilation of N LTRANS jobs"), sin impacto.
- **CI Windows (MSVC):** sin warnings propios. Hay 2 avisos de JUCE porque definimos `JUCE_DISPLAY_SPLASH_SCREEN`, una macro que JUCE 9 ya ignora (B5). Además, GitHub avisa que `checkout@v4`, `cache@v4` y `msvc-dev-cmd@v1` corren sobre Node 20, que está obsoleto (B7).
- **CI macOS (clang, Universal):** sin warnings propios. Aparecen 544 `-Wunused-parameter` dentro de `clap-helpers`. Salen porque los flags de warnings de JUCE se enlazan como `PUBLIC` y llegan a los fuentes del wrapper CLAP (B6). El ruido podría esconder un warning real nuestro.

## 2. Tests automáticos (Catch2)

Resultado: **16/16 casos pasan, con 64 088 aserciones**, en Linux, Windows y macOS (CI `45252018393`).

| Test | Etiquetas | Estado |
|---|---|---|
| MidiPitchControl starts empty | midi | ✔ |
| MidiPitchControl uses last-note priority (5 secciones) | midi | ✔ |
| MidiPitchControl treats velocity 0 as note-off | midi | ✔ |
| MidiPitchControl ignores out-of-range notes and unknown note-offs | midi | ✔ |
| MidiPitchControl holds all 128 notes without overflow | midi | ✔ |
| Processor is a MIDI-receiving audio effect | processor | ✔ |
| Processor bus layouts | processor | ✔ |
| Processor passthrough is bit-exact (5 secciones, bloques de 1 a 4096 muestras) | processor | ✔ |
| Processor reports zero latency in phase 1 | processor | ✔ |
| Processor sanitises NaN and Inf | processor | ✔ |
| Processor publishes received MIDI notes | midi, processor | ✔ |
| Processor exposes the frozen parameter contract (incluye un barrido de los 241 pasos de pitch y formant) | params, processor | ✔ (en el CI #1 fallaba en macOS: B2) |
| State round-trips every parameter | state | ✔ |
| State carries a version number | state | ✔ |
| State ignores foreign or corrupt data | state | ✔ (en el CI #1 fallaba en macOS: B2) |
| Editor can be opened and closed without touching audio | ui | ✔ (en Linux se corre con Xvfb) |

### Cobertura frente al SPEC §7

| Requisito del SPEC §7 | Test en Catch2 | Harness DSP | Estado |
|---|---|---|---|
| Detector: senos de 80 a 1000 Hz con error < 5 ¢ | Falta | Escrito (⏸) | Fase 2. El detector tiene que exponer su f0 (un atómico) para poder medirlo |
| Transponer +7 st sobre un seno, ±5 ¢ | Falta | Escrito (⏸), también ±1, ±7 y ±12 | Fase 3 |
| Formante ±6: f0 estable y centroide en la dirección correcta | Falta | Escrito (✔\* / ⏸) | Fase 3 |
| Link: pitch y formante se mueven juntos | Falta | Escrito (⏸) | Fase 3 |
| Cuantizar: +35 ¢ → < 5 ¢ | Falta | Escrito (⏸), también −40 ¢ | Fase 4 |
| Robot: MIDI 72 con PITCH 0 y la nota MIDI recibida | Falta | Escrito (⏸) | Fase 4 |
| Mix 0 % = null perfecto | Falta (solo hay passthrough bit-exacto) | Escrito (✔\*) | Fase 3 |
| Latencia reportada = medida | **Parcial**: el test solo compara la latencia reportada con una constante, no la mide | Escrito (✔\*) a 44.1, 48 y 96 kHz | Fase 3 |
| Transiciones voiced/unvoiced sin clicks | Falta | Escrito (✔ / ✔\*) | Fases 2 y 3 |
| Sin NaN ni Inf | ✔ | ✔ | Cumplido. Los denormales **no**: B4 |
| Serialización | ✔ (3 tests) | — | Cumplido |
| Bloqueos de parámetros | Falta | — | Fase 6 |
| Harness de render `tests/render` con voz sintética | — | **Creado en esta auditoría** | Cumplido. El generador está en Python (`tools/analysis/signals.py`) |
| Presets: un render por preset | — | Escrito (⏸) | Fase 6: los presets todavía no existen |

**Brecha estructural (G1):** el SPEC pide que estas pruebas sean tests de **Catch2**, y hoy solo existen en el harness de Python, que el CI no ejecuta. La propuesta está en la sección de brechas.

## 3. Validadores

| Validador | Formato | Plataforma | Resultado |
|---|---|---|---|
| pluginval 1.0.4, nivel 10 | VST3 | Linux (Xvfb) | ✔ SUCCESS: 25 grupos, incluidos fuzz de parámetros, estado, buses y editor |
| pluginval 1.0.4, nivel 10 | VST3 | macOS (CI) | ✔ SUCCESS |
| pluginval 1.0.4, nivel 10 | AU | macOS (CI) | ✔ SUCCESS |
| pluginval 1.0.4, nivel 10 | VST3 | Windows (CI) | ✘ **Sin evaluar**: PowerShell no espera a `pluginval.exe` (B3). Mientras se cortaba, la salida parcial mostraba las pruebas pasando |
| pluginval 1.0.4 | CLAP | Linux | N/A: pluginval no carga CLAP ("No types found"). Lo cubre clap-validator |
| Validador VST3 de Steinberg 3.8.0 | VST3 | Linux y macOS | ✔ 47/47 |
| clap-validator 0.3.2 | CLAP | Linux y macOS | ✔ 18 pasan, 0 fallan, 3 omitidas (`preset-discovery-*`: el plugin no expone esa fábrica) |
| auval `-strict` | AU `aumf` | macOS (CI) | ✔ AU VALIDATION SUCCEEDED (carga como efecto MusicEffect) |

Ningún validador reporta fallos atribuibles al plugin.

## 4. Pruebas de DSP con señales sintéticas

Todo se mide sobre el binario real del procesador (`MetamorfoRender`) a 48 kHz, salvo donde se indica otra frecuencia. La voz de prueba es la vocal /a/ sintética a 220 Hz (A3).

| # | Grupo | Prueba | Medido | Esperado | Resultado | Nota |
|---|---|---|---|---|---|---|
| 1 | Detector | Analizador de referencia (YIN numpy) · seno 80 Hz | +0.00 ¢ | \|error\| < 5 ¢ | ✔ | valida la herramienta de medición, no el plugin |
| 2 | Detector | Analizador de referencia (YIN numpy) · seno 150 Hz | +0.01 ¢ | \|error\| < 5 ¢ | ✔ | valida la herramienta de medición, no el plugin |
| 3 | Detector | Analizador de referencia (YIN numpy) · seno 300 Hz | +0.03 ¢ | \|error\| < 5 ¢ | ✔ | valida la herramienta de medición, no el plugin |
| 4 | Detector | Analizador de referencia (YIN numpy) · seno 600 Hz | +0.14 ¢ | \|error\| < 5 ¢ | ✔ | valida la herramienta de medición, no el plugin |
| 5 | Detector | Analizador de referencia (YIN numpy) · seno 1000 Hz | +0.37 ¢ | \|error\| < 5 ¢ | ✔ | valida la herramienta de medición, no el plugin |
| 6 | Detector | Detector del plugin · seno 80 Hz | no expuesto (no existe) | \|error\| < 5 ¢ | ⏸ | (se implementa en la Fase 2) |
| 7 | Detector | Detector del plugin · seno 150 Hz | no expuesto (no existe) | \|error\| < 5 ¢ | ⏸ | (se implementa en la Fase 2) |
| 8 | Detector | Detector del plugin · seno 300 Hz | no expuesto (no existe) | \|error\| < 5 ¢ | ⏸ | (se implementa en la Fase 2) |
| 9 | Detector | Detector del plugin · seno 600 Hz | no expuesto (no existe) | \|error\| < 5 ¢ | ⏸ | (se implementa en la Fase 2) |
| 10 | Detector | Detector del plugin · seno 1000 Hz | no expuesto (no existe) | \|error\| < 5 ¢ | ⏸ | (se implementa en la Fase 2) |
| 11 | Transponer | Voz 220 Hz, PITCH +12 st | 220.00 Hz (-1200.0 ¢) | 440.00 Hz ±5 ¢ | ⏸ | (se implementa en la Fase 3) |
| 12 | Transponer | Voz 220 Hz, PITCH +7 st | 220.00 Hz (-700.0 ¢) | 329.63 Hz ±5 ¢ | ⏸ | (se implementa en la Fase 3) |
| 13 | Transponer | Voz 220 Hz, PITCH +1 st | 220.00 Hz (-100.0 ¢) | 233.08 Hz ±5 ¢ | ⏸ | (se implementa en la Fase 3) |
| 14 | Transponer | Voz 220 Hz, PITCH -1 st | 220.00 Hz (+100.0 ¢) | 207.65 Hz ±5 ¢ | ⏸ | (se implementa en la Fase 3) |
| 15 | Transponer | Voz 220 Hz, PITCH -7 st | 220.00 Hz (+700.0 ¢) | 146.83 Hz ±5 ¢ | ⏸ | (se implementa en la Fase 3) |
| 16 | Transponer | Voz 220 Hz, PITCH -12 st | 220.00 Hz (+1200.0 ¢) | 110.00 Hz ±5 ¢ | ⏸ | (se implementa en la Fase 3) |
| 17 | Transponer | Seno 220 Hz, PITCH +7 st (SPEC §7) | 220.00 Hz (-700.0 ¢) | 329.63 Hz ±5 ¢ | ⏸ | (se implementa en la Fase 3) |
| 18 | Formante | FORMANT +6, PITCH 0: f0 estable | +0.0 ¢ | \|Δf0\| < 5 ¢ | ✔* | (trivial: la función llega en la Fase 3) |
| 19 | Formante | FORMANT +6, PITCH 0: centroide | 733 Hz (+0.0 %) | sube > 3 % (entrada 733 Hz) | ⏸ | (se implementa en la Fase 3) |
| 20 | Formante | FORMANT -6, PITCH 0: f0 estable | +0.0 ¢ | \|Δf0\| < 5 ¢ | ✔* | (trivial: la función llega en la Fase 3) |
| 21 | Formante | FORMANT -6, PITCH 0: centroide | 733 Hz (+0.0 %) | baja > 3 % (entrada 733 Hz) | ⏸ | (se implementa en la Fase 3) |
| 22 | Link | PITCH +5: f0 con Link OFF / ON | -500.0 ¢ / -500.0 ¢ | ambos ±5 ¢ de +5 st | ⏸ | (se implementa en la Fase 3) |
| 23 | Link | PITCH +5: centroide relativo OFF / ON | ×1.000 / ×1.000 | OFF ≈ ×1.00 (±5 %), ON > ×1.15 (≈ ×1.33) | ⏸ | (se implementa en la Fase 3) |
| 24 | Cuantizar | Voz A3 +35 ¢ | +35.0 ¢ de A3 | \|error\| < 5 ¢ | ⏸ | (se implementa en la Fase 4) |
| 25 | Cuantizar | Voz A3 -40 ¢ | -40.0 ¢ de A3 | \|error\| < 5 ¢ | ⏸ | (se implementa en la Fase 4) |
| 26 | Robot | PITCH 0, sin MIDI → MIDI 72 (523.25 Hz) | -1500.0 ¢ | \|error\| < 5 ¢ | ⏸ | (se implementa en la Fase 4) |
| 27 | Robot | PITCH +3, sin MIDI → MIDI 75 | -1800.0 ¢ | \|error\| < 5 ¢ | ⏸ | (se implementa en la Fase 4) |
| 28 | Robot | Nota MIDI 64 sostenida | -700.0 ¢ | \|error\| < 5 ¢ | ⏸ | (se implementa en la Fase 4) |
| 29 | Robot | Nota MIDI 67 sostenida | -1000.0 ¢ | \|error\| < 5 ¢ | ⏸ | (se implementa en la Fase 4) |
| 30 | Robot | Nota MIDI 48 sostenida | +900.0 ¢ | \|error\| < 5 ¢ | ⏸ | (se implementa en la Fase 4) |
| 31 | Drive | DRIVE 0 transparente (null vs entrada) | -inf dBFS | < −90 dBFS | ✔* | (trivial: la función llega en la Fase 5) |
| 32 | Drive | DRIVE 6: H2 vs H3 (rel. H1) | H2 -199.5 dB, H3 -181.8 dB | H2 > H3 y H2 > −60 dB | ⏸ | (se implementa en la Fase 5) |
| 33 | Drive | DRIVE 6: cambio de nivel RMS | +0.00 dB | ±1.5 dB | ✔* | compensación automática (trivial: la función llega en la Fase 5) |
| 34 | Drive | DRIVE 12: H2 vs H3 (rel. H1) | H2 -199.5 dB, H3 -181.8 dB | H2 > H3 y H2 > −60 dB | ⏸ | (se implementa en la Fase 5) |
| 35 | Drive | DRIVE 12: cambio de nivel RMS | +0.00 dB | ±1.5 dB | ✔* | compensación automática (trivial: la función llega en la Fase 5) |
| 36 | Latencia | 44.1 kHz: medida (dry / wet) vs reportada | 0 / 0 vs 0 muestras (0.0 ms) | iguales (±1 muestra), fija, ≤ 30 ms (máx. 60) | ✔* | en Fase 1 el motor no existe: latencia 0 (trivial: la función llega en la Fase 3) |
| 37 | Mix | MIX 0 % (PITCH +7) null vs entrada, alineado por latencia | −∞ (null perfecto) | null perfecto (−∞ / < −120 dBFS) | ✔* | (trivial: la función llega en la Fase 3) |
| 38 | Latencia | 48 kHz: medida (dry / wet) vs reportada | 0 / 0 vs 0 muestras (0.0 ms) | iguales (±1 muestra), fija, ≤ 30 ms (máx. 60) | ✔* | en Fase 1 el motor no existe: latencia 0 (trivial: la función llega en la Fase 3) |
| 39 | Latencia | 96 kHz: medida (dry / wet) vs reportada | 0 / 0 vs 0 muestras (0.0 ms) | iguales (±1 muestra), fija, ≤ 30 ms (máx. 60) | ✔* | en Fase 1 el motor no existe: latencia 0 (trivial: la función llega en la Fase 3) |
| 40 | Clicks | Transiciones voz/silencio (PITCH 0) | 0 (entrada 0) | ≤ clicks de la entrada | ✔ |  |
| 41 | Clicks | Transiciones voz/silencio (PITCH +5) | 0 (entrada 0) | ≤ clicks de la entrada | ✔* | (trivial: la función llega en la Fase 3) |
| 42 | Clicks | Cambios de modo durante la reproducción (5 cambios) | 0 (entrada 0) | ≤ clicks de la entrada | ✔* | (trivial: la función llega en la Fase 4) |
| 43 | Clicks | Saltos bruscos de parámetros (zipper) | 0 (entrada 0) | ≤ clicks de la entrada | ✔* | requiere parámetros suavizados (trivial: la función llega en la Fase 3) |
| 44 | Robustez | silencio · buffer 16 · mono/estéreo/mono→estéreo | NaN 0, Inf 0, denormales 0 | 0 / 0 / 0 | ✔ | salida idéntica a la entrada (passthrough) |
| 45 | Robustez | silencio · buffer 64 · mono/estéreo/mono→estéreo | NaN 0, Inf 0, denormales 0 | 0 / 0 / 0 | ✔ | salida idéntica a la entrada (passthrough) |
| 46 | Robustez | silencio · buffer 512 · mono/estéreo/mono→estéreo | NaN 0, Inf 0, denormales 0 | 0 / 0 / 0 | ✔ | salida idéntica a la entrada (passthrough) |
| 47 | Robustez | silencio · buffer 4096 · mono/estéreo/mono→estéreo | NaN 0, Inf 0, denormales 0 | 0 / 0 / 0 | ✔ | salida idéntica a la entrada (passthrough) |
| 48 | Robustez | silencio · buffer irregular 1–4096 · mono/estéreo/mono→estéreo | NaN 0, Inf 0, denormales 0 | 0 / 0 / 0 | ✔ | salida idéntica a la entrada (passthrough) |
| 49 | Robustez | ruido blanco · buffer 16 · mono/estéreo/mono→estéreo | NaN 0, Inf 0, denormales 0 | 0 / 0 / 0 | ✔ | salida idéntica a la entrada (passthrough) |
| 50 | Robustez | ruido blanco · buffer 64 · mono/estéreo/mono→estéreo | NaN 0, Inf 0, denormales 0 | 0 / 0 / 0 | ✔ | salida idéntica a la entrada (passthrough) |
| 51 | Robustez | ruido blanco · buffer 512 · mono/estéreo/mono→estéreo | NaN 0, Inf 0, denormales 0 | 0 / 0 / 0 | ✔ | salida idéntica a la entrada (passthrough) |
| 52 | Robustez | ruido blanco · buffer 4096 · mono/estéreo/mono→estéreo | NaN 0, Inf 0, denormales 0 | 0 / 0 / 0 | ✔ | salida idéntica a la entrada (passthrough) |
| 53 | Robustez | ruido blanco · buffer irregular 1–4096 · mono/estéreo/mono→estéreo | NaN 0, Inf 0, denormales 0 | 0 / 0 / 0 | ✔ | salida idéntica a la entrada (passthrough) |
| 54 | Robustez | seno 0 dBFS · buffer 16 · mono/estéreo/mono→estéreo | NaN 0, Inf 0, denormales 0 | 0 / 0 / 0 | ✔ | salida idéntica a la entrada (passthrough) |
| 55 | Robustez | seno 0 dBFS · buffer 64 · mono/estéreo/mono→estéreo | NaN 0, Inf 0, denormales 0 | 0 / 0 / 0 | ✔ | salida idéntica a la entrada (passthrough) |
| 56 | Robustez | seno 0 dBFS · buffer 512 · mono/estéreo/mono→estéreo | NaN 0, Inf 0, denormales 0 | 0 / 0 / 0 | ✔ | salida idéntica a la entrada (passthrough) |
| 57 | Robustez | seno 0 dBFS · buffer 4096 · mono/estéreo/mono→estéreo | NaN 0, Inf 0, denormales 0 | 0 / 0 / 0 | ✔ | salida idéntica a la entrada (passthrough) |
| 58 | Robustez | seno 0 dBFS · buffer irregular 1–4096 · mono/estéreo/mono→estéreo | NaN 0, Inf 0, denormales 0 | 0 / 0 / 0 | ✔ | salida idéntica a la entrada (passthrough) |
| 59 | Robustez | ruido denormal · buffer 16 · mono/estéreo/mono→estéreo | NaN 0, Inf 0, denormales 144000 | 0 / 0 / 0 | ✘ | salida idéntica a la entrada (passthrough) |
| 60 | Robustez | ruido denormal · buffer 64 · mono/estéreo/mono→estéreo | NaN 0, Inf 0, denormales 144000 | 0 / 0 / 0 | ✘ | salida idéntica a la entrada (passthrough) |
| 61 | Robustez | ruido denormal · buffer 512 · mono/estéreo/mono→estéreo | NaN 0, Inf 0, denormales 144000 | 0 / 0 / 0 | ✘ | salida idéntica a la entrada (passthrough) |
| 62 | Robustez | ruido denormal · buffer 4096 · mono/estéreo/mono→estéreo | NaN 0, Inf 0, denormales 144000 | 0 / 0 / 0 | ✘ | salida idéntica a la entrada (passthrough) |
| 63 | Robustez | ruido denormal · buffer irregular 1–4096 · mono/estéreo/mono→estéreo | NaN 0, Inf 0, denormales 144000 | 0 / 0 / 0 | ✘ | salida idéntica a la entrada (passthrough) |
| 64 | Robustez | ruido blanco · parámetros extremos · buffers irregulares | NaN 0, Inf 0, denormales 0, pico 0.300 | 0 / 0 / 0, sin explosión | ✔* | (trivial: la función llega en la Fase 5) |
| 65 | Robustez | seno 0 dBFS · parámetros extremos · buffers irregulares | NaN 0, Inf 0, denormales 0, pico 1.000 | 0 / 0 / 0, sin explosión | ✔* | (trivial: la función llega en la Fase 5) |
| 66 | Robustez | Sample rate 44.1 kHz | NaN 0, Inf 0 | 0 / 0 | ✔ |  |
| 67 | Robustez | Sample rate 96 kHz | NaN 0, Inf 0 | 0 / 0 | ✔ |  |
| 68 | Robustez | Sample rate 192 kHz | NaN 0, Inf 0 | 0 / 0 | ✔ |  |
| 69 | Renders | 11 WAV de escucha en tests/render/output/ | 11 archivos | uno por modo + variantes | ✔ | en Fase 1 todos suenan igual que la entrada |
| 70 | Renders | Un WAV por preset de fábrica (16) | no existen presets | 16 archivos | ⏸ | (se implementa en la Fase 6) |
| 71 | CPU | 1 instancia, 48 kHz, buffer 128 (134 s de audio) | 0.011 % de un núcleo | < 2 % | ✔ | contenedor Linux; sin motor DSP todavía (se re-mide en Fases 2–5) |
| 72 | CPU | 1 instancia, 48 kHz, buffer 16 (peor caso de overhead) | 0.031 % de un núcleo | < 2 % | ✔ |  |
| 73 | Tiempo real | Alocaciones dentro de processBlock (111 renders) | 0 (sonda activa: 449 alocaciones fuera) | 0 | ✔ |  |

**Lectura rápida de la tabla:**
- **Fallos reales (✘): 5 filas, todas el mismo bug (B4).** Si la entrada trae valores denormales, la salida los conserva: 144 000 muestras denormales por render. El sanitizador elimina NaN e Inf, pero no los denormales, y el SPEC §4 pide salida sin denormales.
- **El analizador de referencia es fiable.** Mide los senos de 80 a 1000 Hz con un error de +0.00 a +0.37 ¢, de modo que las mediciones en cents son confiables con margen frente al umbral de 5 ¢.
- **Las filas ⏸ muestran que hoy no hay procesamiento.** Por ejemplo, Transponer +7 da −700 ¢ porque la salida es igual a la entrada. Cada una pasará a ✔ o ✘ cuando llegue su fase.
- **Robustez:** silencio, ruido, 0 dBFS, buffers de 16, 64, 512 y 4096 muestras e irregulares (1 a 4096), layouts mono, estéreo y mono→estéreo, y sample rates de 44.1, 96 y 192 kHz. No aparece ningún NaN ni Inf, y la salida es bit-idéntica a la entrada.
- **Tiempo real:** 0 alocaciones dentro de `processBlock` en 111 renders. La sonda funciona: registró 449 alocaciones fuera de `processBlock`.

## 5. Renders para escuchar

Están en `tests/render/output/`. Los genera `run_audit.py`, se reproducen con cualquier reproductor y no se suben al repo.

| Archivo | Ajustes |
|---|---|
| `00_entrada_voz_sintetica.wav` | Entrada: frase de 4 notas con vibrato y silencios |
| `01_transponer_+7.wav` | Transponer, PITCH +7 |
| `02_transponer_-12.wav` | Transponer, PITCH −12 |
| `03_transponer_+12.wav` | Transponer, PITCH +12 |
| `04_formante_-3_coros.wav` | FORMANT −3 (cuerpo para coros) |
| `05_link_on_+5.wav` | Link ON, PITCH +5 |
| `06_cuantizar.wav` | Cuantizar, PITCH 0 |
| `07_robot_pitch0.wav` | Robot, PITCH 0 (debería quedar en MIDI 72) |
| `08_robot_melodia_midi.wav` | Robot con la melodía MIDI 60-64-67-72 |
| `09_drive_8.wav` | DRIVE 8 |
| `10_mix_50_+7.wav` | MIX 50 %, PITCH +7 |
| Presets de fábrica | ⏸ No existen todavía (Fase 6) |

**Qué escuchar primero.** En la Fase 1 **todos suenan idénticos a la entrada**, así que no hay nada que evaluar de oído. Basta con escuchar `00` y `07` para confirmar que el passthrough está limpio. Cuando exista el motor, este es el orden recomendado:
1. `04` formante −3: es el uso más común y el SPEC exige naturalidad.
2. `01` +7.
3. `08` Robot con melodía MIDI.
4. `06` Cuantizar.
5. `02` y `03` ±12, los casos extremos de PSOLA.
6. `10` mix 50 % (fase y flanger).
7. `05` Link.
8. `09` Drive.

## 6. Revisión de código (reglas de tiempo real)

Revisé `processBlock` → `handleMidi` → `processChunk`, el editor y `MidiPitchControl`.

| Regla del SPEC §4 | Resultado | Evidencia |
|---|---|---|
| Sin alocaciones en `processBlock` | ✔ | Revisión del código + sonda de `operator new`: 0 en 111 renders |
| Sin locks | ✔ | Ni `CriticalSection`, ni mutex, ni `copyState` en la ruta de audio. El estado se publica con atómicos `relaxed` |
| Sin logs, I/O ni excepciones | ✔ | Sin `DBG`, `Logger`, `std::cout` ni `throw`. Las funciones de la ruta de audio son `noexcept` |
| `ScopedNoDenormals` | ✔ | Está al inicio de `processBlock` |
| Salida sanitizada | ⚠ Parcial | Elimina NaN e Inf, pero **deja pasar denormales de la entrada** (B4) |
| Sin estado estático mutable | ✔ en código propio | Solo hay `static constexpr`. En código de terceros, `clap_properties::building_clap` es estático, pero el wrapper CLAP solo lo escribe al construir la instancia en el hilo de mensajes. Cada formato es un binario aparte, así que no se comparte entre VST3 y CLAP. Aceptable |
| Parámetros suavizados | N/A | Ningún parámetro afecta al audio todavía. **No existe infraestructura de suavizado** (G2): en la Fase 3 hay que leer los parámetros con punteros atómicos cacheados y `SmoothedValue` |
| Bloques mayores que el tamaño preparado | ✔ | Se procesan en trozos. Probado con bloques de 1 a 4096 sobre un tamaño preparado de 16 y de 512 |
| UI por atómicos; cerrar el editor no afecta al audio | ✔ | Test `Editor can be opened and closed…` + timer a 30 Hz que solo lee atómicos |
| MIDI | ⚠ Por bloque | Las notas se procesan una vez por bloque, sin precisión de muestra. Está previsto para la Fase 4 y anotado en el ROADMAP |

## 7. Bugs, ordenados por gravedad

| ID | Gravedad | Bug | Impacto | Corrección propuesta | Estado |
|---|---|---|---|---|---|
| **B1** | Alta | El script del CI busca el validador VST3 en `bin/Release/`. En Windows está en `bin/` | El CI de Windows se detenía antes de validar y no publicaba binarios | Buscar `validator*` en todo `vst3build/bin` | ✔ Corregido (`0ffbf8f`), a petición tuya, antes de la auditoría |
| **B2** | Media | En arm64, los pasos de 0.1 de los parámetros dejaban un residuo (`0.000000179` en vez de `0`), y 2 tests comparaban con `==` exacto | El CI de macOS fallaba. En la Fase 3, la lógica "pitch == 0" habría fallado en Mac | Ajustar el valor en unidades decimales (`steppedRange`) + test de regresión con los 241 pasos | ✔ Corregido (`0ffbf8f`) |
| **B3** | **Alta** | PowerShell no espera a `pluginval.exe`, que es una aplicación de interfaz gráfica: `$LASTEXITCODE` queda vacío y el script aborta | En Windows **no se ejecutan** pluginval, el validador VST3 ni clap-validator, y **no se publican los binarios de Windows** que necesitas para FL | Lanzar pluginval con `Start-Process -Wait -NoNewWindow -PassThru` y comprobar `.ExitCode`, redirigiendo la salida a un log que se muestre después | ✔ Corregido al final de la auditoría: el script espera a pluginval con `Start-Process -Wait`. Verificado en el CI: pluginval da SUCCESS en Windows |
| **B4** | Baja | El sanitizador no elimina los denormales que llegan en la entrada | Incumple "sin denormales" del SPEC. Un host que envía denormales los recibe de vuelta; pueden costar CPU aguas abajo | En `processChunk`, convertir a 0 los valores con `abs(x) < FLT_MIN`, en el mismo bucle que NaN e Inf. Añadir un test en Catch2 | ✔ Corregido + test `Processor flushes denormals coming from the input`. El harness ahora da 0 denormales |
| **B5** | Baja | Se define `JUCE_DISPLAY_SPLASH_SCREEN`, que JUCE 9 ignora | Warning en MSVC y clang | Quitar la definición de `CMakeLists.txt` | ✔ Corregido |
| **B6** | Baja | Los flags de warnings de JUCE se enlazan `PUBLIC` y llegan al wrapper CLAP | 544 warnings de terceros en macOS que pueden ocultar warnings reales | Tras analizarlo, la causa real era otra: los warnings salen del header `clap-juce-extensions.h`, que incluye `PluginProcessor.h`. Corrección aplicada: marcar `clap_juce_extensions`, `clap` y `clap-helpers` como `SYSTEM` (CMake 3.25) | ✔ Corregido. Verificado con clang: esa unidad pasa de 136 warnings a 0 |
| **B7** | Baja | Varias acciones de GitHub corren sobre Node 20, que está obsoleto | Avisos hoy; fallarán cuando GitHub retire Node 20 | Actualizar `actions/checkout`, `actions/cache` y `actions/upload-artifact` a v5 (Node 24) | ✔ Corregido. `ilammy/msvc-dev-cmd` no tiene versión para Node 24 y su aviso permanece |

### Brechas (no son bugs: funciones o pruebas que faltan, con su fase)

| ID | Brecha | Propuesta | Fase |
|---|---|---|---|
| G1 | Las pruebas DSP del §7 existen en Python, pero no en Catch2, y el CI no corre el harness | En cada fase, portar sus pruebas a Catch2 con un generador de voz sintética en C++ (`tests/unit/TestSignals.h`) y añadir al CI un paso que corra `run_audit.py --phase N` y falle ante cualquier ✘ | 2 a 6 |
| G2 | No hay infraestructura de suavizado de parámetros | Leer los parámetros con `getRawParameterValue` cacheado en el constructor y usar `juce::SmoothedValue` con una rampa de 20 a 50 ms por parámetro continuo | 3 |
| G3 | El detector no es medible desde fuera | Publicar la f0 y la confianza en atómicos y añadirlas al JSON de `MetamorfoRender` | 2 |
| G4 | El test de latencia no mide; solo compara con una constante | Test en Catch2: impulso con MIX 0 %, buscar el pico y compararlo con `getLatencySamples()` a 44.1, 48 y 96 kHz | 3 |
| G5 | El MIDI se procesa por bloque | Dividir el bloque en los eventos MIDI (precisión de muestra) | 4 |
| G6 | La CPU medida no es representativa: no hay motor y se midió en un contenedor | Volver a medir en cada fase. Añadir al CI un benchmark con límite (< 2 % en buffer 128) | 2 a 5 |
| G7 | pluginval no valida CLAP | Mantener clap-validator como validador oficial del CLAP. Anotado en el ROADMAP | — |

## 8. Pruebas manuales en FL Studio

El checklist completo y ordenado está en [TESTING_FL.md](TESTING_FL.md), sección Fase 1. Las pruebas de Robot, Cuantizar y los presets se agregan ahí en sus fases.

## 9. Re-ejecución tras las correcciones

Las correcciones B3 a B7 se aplicaron **después** de completar el diagnóstico, como pediste. La tabla de la sección 4 conserva los resultados del diagnóstico, antes de corregir. Al repetir todo:

| Prueba | Antes | Después |
|---|---|---|
| Compilación Release (Linux) | ✔ 0 warnings propios | ✔ 0 warnings propios |
| Tests Catch2 | 16/16 | **17/17**, con el nuevo test de denormales |
| pluginval nivel 10, VST3 (Linux) | ✔ SUCCESS | ✔ SUCCESS |
| Validador VST3 de Steinberg | ✔ 47/47 | ✔ 47/47 |
| clap-validator | ✔ 18/0/3 | ✔ 18/0/3 |
| Harness DSP | ✔ 28 · ✔\* 14 · ✘ 5 · ⏸ 26 | **✔ 33 · ✔\* 14 · ✘ 0 · ⏸ 26** |
| Denormales en la salida (ruido denormal, 15 combinaciones) | 144 000 por render | **0** |
| Warnings de clap-helpers con clang | 136 por unidad de compilación | **0** |
| CI Windows y macOS | Windows ✘ (B3) · macOS ✔ | **✔ los dos** (run `45256536604`, commit `f0bf27e`) |
| pluginval nivel 10, VST3 (Windows, CI) | Sin evaluar (B3) | **✔ SUCCESS** |
| Validador VST3 de Steinberg (Windows, CI) | Sin evaluar | **✔ 47/47** |
| clap-validator (Windows, CI) | Sin evaluar | **✔ 17 pasan · 0 fallan · 4 omitidas**: 3 de preset-discovery + `scan-rtld-now`, que solo aplica a Unix |
| Artefactos del CI | Solo macOS | `Metamorfo-win-VST3`, `Metamorfo-win-CLAP`, `Metamorfo-win-Standalone`, `Metamorfo-mac` |

> Nota: en el log de clap-validator de Windows aparece `CLAP asked for plugin_id 'com.metamorfoaudio.metamorfox1'`. Es esperado: una de las pruebas pide a propósito un ID que no existe para verificar que el plugin lo rechaza, y la prueba pasa.

## 10. Corrección automática (2026-11-24, sin prueba manual en FL)

No pudiste probar en FL, así que automaticé todo lo que se puede verificar sin FL y cerré las brechas abordables sin empezar la Fase 2.

### Lo que se agregó

| Pieza | Qué verifica |
|---|---|
| `tests/unit/HostBehaviourTests.cpp` (6 casos) | Comportamientos típicos de FL y otros DAWs: **12 instancias procesando en paralelo** en hilos distintos; **cambio de sample rate en caliente** (44.1 → 96 → 48 → 192 → 44.1 kHz) sin `releaseResources`; `processBlock` **antes de `prepareToPlay`** y con **0 muestras**; **guardar y cargar estado 200 veces mientras suena** con automatización desde el hilo de audio; **abrir y cerrar el editor 50 veces mientras suena**; MIDI en los bordes de bloque (última muestra, bloques de 1 muestra) |
| `tests/unit/LatencyTests.cpp` (G4) | **Latencia medida = reportada** (impulso; MIX 0 % y 100 %; 44.1, 48 y 96 kHz) y tope de 60 ms del SPEC §4.5 |
| `tests/host/` → `MetamorfoHostProbe` | Carga los **binarios compilados** como un DAW. **VST3** con el hosting de JUCE: efecto (no instrumento), categoría Fx, **bus de entrada MIDI**, layouts, procesamiento con MIDI, estado entre instancias. **CLAP** con un host mínimo propio: id, vendor, feature audio-effect, **`clap.note-ports` con 1 puerto de entrada y dialecto MIDI** (lo que FL necesita para enrutar el MIDI Out), puertos de audio estéreo, `process()` con eventos de nota y MIDI crudo, estado guardado → cargado → guardado idéntico |
| Presets `asan` y `tsan` (Linux y macOS) | AddressSanitizer + UBSan + detección de fugas, y ThreadSanitizer |
| `run_audit.py --fail-on-x` + paso en el CI de macOS | El harness DSP corre en cada push y falla ante cualquier ✘ (G1 parcial). La tabla se publica como artefacto `dsp-audit` |
| Job `linux-sanitizers` en el CI | ASan+UBSan sobre todos los tests (incluidos los host probes) y TSan sobre los tests de hilos |

### Resultados (Linux local)

| Prueba | Resultado |
|---|---|
| CTest Release | ✔ **26/26**: 24 casos de Catch2 + `host_probe_vst3` + `host_probe_clap` |
| Host probe VST3 | ✔ 20/20 comprobaciones |
| Host probe CLAP | ✔ 27/27 comprobaciones, incluido el **puerto de notas MIDI** |
| ASan + UBSan + LeakSanitizer (Debug) | ✔ 26/26, sin reportes |
| ASan sobre el harness DSP completo | ✔ 73 pruebas, 0 ✘, sin reportes |
| ThreadSanitizer | ✔ 24/24, **sin data races**. Control negativo: TSan sí detecta una carrera de prueba en este entorno |
| `run_audit.py --phase 1 --fail-on-x` | ✔ código de salida 0. Control negativo con `--phase 9`: código 1 |
| pluginval nivel 10 · validador VST3 · clap-validator | ✔ SUCCESS · 47/47 · 18/0/3 |
| **CI** (run `45309729521`, commit `1ecdad8`) | ✔ **los 3 jobs en verde**: Windows (tests + host probes + validadores), macOS (tests + host probes + auditoría DSP + auval/pluginval/validadores) y linux-sanitizers (ASan/UBSan + TSan) |

### Hallazgos

| ID | Gravedad | Hallazgo | Corrección |
|---|---|---|---|
| **B8** | Baja (solo tests) | El test `Editor can be opened and closed…` de la auditoría borraba el editor **sin** llamar a `editorBeingDeleted()`. En JUCE 9 esa llamada le toca al host, no al destructor, así que el procesador quedaba con un puntero colgante. **El plugin no estaba afectado**, porque los wrappers de JUCE sí la hacen. Lo descubrió el nuevo test de 50 aperturas | ✔ Los dos tests de editor siguen ahora el protocolo de un host real. Los hilos de prueba hacen `join` aunque falle una aserción (`StopAndJoin`) |
| I1 | Informativo | En Debug, el wrapper CLAP de terceros dispara su propia aserción `FIXME: Handle bypass and deactivated states` (`clap-juce-wrapper.cpp:1824`) en cada `process()` | No se corrige. Es un marcador interno de clap-juce-extensions que no existe en Release ni afecta al audio. Se anota en el ROADMAP para seguirlo cuando se actualice la dependencia |

### Qué ya no depende de FL y qué sí

- **Automatizado:**
  - carga del binario VST3 y CLAP en un host
  - registro como efecto
  - **declaración de la entrada MIDI en VST3 y en CLAP**
  - layouts, passthrough y estado
  - instancias en paralelo
  - cambios de sample rate y de buffer
  - editor con el audio en marcha
  - ausencia de alocaciones, carreras y errores de memoria
- **Solo se puede verificar en FL:**
  - que el escáner de FL lo detecte
  - el enrutado real de un canal **MIDI Out → port 1** hacia el wrapper
  - guardar y reabrir el `.flp`
  - export frente a reproducción
  - el escalado DPI
  - La sección "Prueba rápida" de [TESTING_FL.md](TESTING_FL.md) reúne solo esos puntos (unos 5 minutos).
