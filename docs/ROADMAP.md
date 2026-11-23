# ROADMAP — Metamorfo

La especificación completa está en [SPEC.md](SPEC.md). Este archivo lleva el estado de cada fase, lo que queda pendiente y las decisiones tomadas.

## Fases

| Fase | Estado | Notas |
|---|---|---|
| 1. Esqueleto + MIDI | ✅ Implementada · ⏳ falta tu prueba en FL | CMake + JUCE 9 + CLAP, passthrough, MIDI, CI, docs |
| 2. Detector de tono | Pendiente | YIN-FFT (+ MPM como validador), voicing, display de nota detectada |
| 3. Motor de tono y formantes | Pendiente | PSOLA, formante, Link, Transponer, Mix alineado, latencia fija |
| 4. Cuantizar, Robot y MIDI | Pendiente | Reutiliza `MidiPitchControl`; añade precisión de muestra y portamento |
| 5. Drive | Pendiente | Tríodo, sobremuestreo 2× IIR, compensación de nivel |
| 6. UI final + presets | Pendiente | Diseño del §8, presets, bloqueos, ayuda MIDI |
| 7. Multi-DAW + distribución | Pendiente | DAW_MATRIX, instaladores, guía de usuario |

## Pendiente para aceptar la Fase 1

- [ ] Prueba manual en FL Studio en Windows con VST3 y CLAP ([TESTING_FL.md](TESTING_FL.md), sección Fase 1).
- [ ] Prueba manual en FL Studio en macOS, si tienes Mac. Si no, queda pendiente hasta tener acceso a uno.
- [ ] CI en verde en `windows-latest` y `macos-latest`. Lo verifico en cada push.
- [ ] Renombrar el repositorio de GitHub a `metamorfo` (lo haces tú: Settings → Repository name). Es un requisito legal del §1.

## Pendiente para fases posteriores (identificado en la Fase 1)

- **Fase 2:** decidir la f0 mínima frente a la latencia. Con 60 Hz se estiman entre 35 y 45 ms; el objetivo de ≤ 30 ms pide unos 80 Hz. Se decide con benchmarks y con tu elección.
- **Fase 2:** comparar PFFFT (BSD) con `juce::dsp::FFT` por rendimiento en Windows.
- **Fase 3:** prueba A/B de formante: re-muestreo de grano frente a envolvente cepstral, con el harness de render.
- **Fase 3:** plan B si PSOLA no queda utilizable en ±12 st: phase vocoder con preservación de envolvente para desplazamientos grandes. Se consulta antes de implementarlo.
- **Fase 4:** procesar el MIDI con precisión de muestra. En la Fase 1 se procesa por bloque.
- **Fase 4:** evaluar `CLAP_PROCESS_EVENTS_RESOLUTION_SAMPLES` para automatización precisa en CLAP.
- **Fase 6:** probar el escalado DPI en FL Studio en Windows. Desde JUCE 8.0.13 hay reportes de cambios en la conciencia de DPI del proceso host.
- **Fase 6:** comprobar que `getProgramName` y el sistema de presets propio no chocan con los presets nativos de FL.
- **CI:** añadir ccache o sccache si el tiempo de build crece.
- **CI:** la slice x86_64 de macOS solo se verifica con `lipo`, porque el runner es arm64. Probarla en un Mac Intel o con Rosetta cuando sea posible.

## Fuera de alcance

- AAX (requiere SDK y firma de Avid) y VST2 (SDK descontinuado). Fuera de alcance según el SPEC §2.
- Notarización de Apple: requiere una cuenta de Apple Developer de pago. Por ahora se usa firma ad-hoc más `xattr`.
- Linux: no es objetivo. El código compila en Linux y se usa como chequeo local, pero no se distribuye.

## Excepciones de validación (con justificación)

| Validador | Prueba | Resultado | Motivo |
|---|---|---|---|
| clap-validator 0.3.2 | `preset-discovery-*` (3) | Omitidas por el validador | El plugin no implementa la fábrica de preset-discovery de CLAP. Se reevaluará en la Fase 6. |

No hay ninguna otra excepción. pluginval (nivel 10) y el validador VST3 de Steinberg (47/47) pasan completos.

## Decisiones

| Fecha | Decisión | Motivo |
|---|---|---|
| Fase 1 | JUCE 9.0.6 en lugar de JUCE 8 (aprobado por el dueño) | La rama 8 solo recibe parches de seguridad. clap-juce-extensions ya soporta JUCE 9. Respaldo: 9.0.0. |
| Fase 1 | Licencia AGPLv3 | Elección del dueño. Permite usar JUCE sin licencia comercial. |
| Fase 1 | Identidad: Metamorfo Audio / `Mtmf` / `Mmf1` / `com.metamorfoaudio.metamorfo` | Elección del dueño. Está congelada. |
| Fase 1 | CPM con dependencias fijadas por tag o commit vía git | Evita los submódulos recursivos (clap-juce-extensions anida `clap` y `clap-helpers`). |
| Fase 1 | Los 8 parámetros del §5 se crean ya, en orden fijo | FL guarda la automatización por índice. Fijar el orden pronto evita romper `.flp`. |
| Fase 1 | Runtime MSVC estático (`/MT`) | El usuario no necesita instalar el VC++ Redistributable. |
| Fase 1 | macOS 12.0 o superior, binario Universal, firma ad-hoc | Es el defecto de JUCE 9. No hay cuenta de pago para notarizar. |
| Fase 1 | `getProgramName(0)` devuelve "Default" | Lo exige el validador VST3 de Steinberg. Los presets propios llegan en la Fase 6. |
| Fase 1 | Artefactos de macOS en zip creados con `ditto` | `upload-artifact` no conserva los permisos ni la estructura de los bundles. |
