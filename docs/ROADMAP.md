# ROADMAP — Metamorfo

La especificación completa está en [SPEC.md](SPEC.md). Este archivo lleva el estado de cada fase, lo que queda pendiente y las decisiones tomadas.

## Fases

| Fase | Estado | Notas |
|---|---|---|
| 1. Esqueleto + MIDI | ✅ Implementada · ⏳ falta tu prueba en FL | CMake + JUCE 9 + CLAP, passthrough, MIDI, CI, docs |
| 2. Detector de tono | ✅ Implementada (2026-11-24) · ⏳ falta tu prueba en FL | YIN diezmado + refinamiento a la frecuencia completa, voicing con 3 indicadores, display ENTRADA/SALIDA |
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

- ~~**Fase 2:** f0 mínima frente a latencia~~ → decidido en automático (ver Decisiones): f0 de 60 a 1200 Hz y latencia del motor **2 × periodo máximo = 33,3 ms**.
- ~~**Fase 2:** PFFFT frente a `juce::dsp::FFT`~~ → no hace falta FFT: YIN directo sobre la señal diezmada a unos 11-12 kHz, con sumas vectorizadas, cuesta 0,42 % de un núcleo.
- **Fase 2 (pendiente menor):** MPM como validador no se implementó. YIN ya prefiere el periodo más corto por debajo del umbral, que evita los errores de sub-octava, y la mediana de 5 más la histéresis cubren los saltos de octava. Los tests con voz sintética de E2 a E5 no muestran errores de octava. Se reevaluará si aparecen en voz real.
- **Fase 3:** prueba A/B de formante: re-muestreo de grano frente a envolvente cepstral, con el harness de render.
- **Fase 3:** plan B si PSOLA no queda utilizable en ±12 st: phase vocoder con preservación de envolvente para desplazamientos grandes. Se consulta antes de implementarlo.
- **Fase 4:** procesar el MIDI con precisión de muestra. En la Fase 1 se procesa por bloque.
- **Fase 4:** evaluar `CLAP_PROCESS_EVENTS_RESOLUTION_SAMPLES` para automatización precisa en CLAP.
- **Fase 6:** probar el escalado DPI en FL Studio en Windows. Desde JUCE 8.0.13 hay reportes de cambios en la conciencia de DPI del proceso host.
- **Fase 6:** comprobar que `getProgramName` y el sistema de presets propio no chocan con los presets nativos de FL.
- **Auditoría de pruebas (2026-11-23, [TEST_REPORT.md](TEST_REPORT.md)):** brechas G1 a G7 por resolver en sus fases:
  - **G1 (Fases 2 a 6):** portar cada prueba DSP del SPEC §7 a Catch2, con un generador de voz en C++. *Hecho el 2026-11-24:* el CI de macOS ya corre `run_audit.py --phase 1 --fail-on-x`. **Subir `--phase` en `build.yml` al cerrar cada fase.**
  - **G2 (Fase 3):** infraestructura de suavizado: punteros atómicos cacheados + `juce::SmoothedValue`.
  - **G3 (Fase 2):** publicar la f0 y la confianza del detector en atómicos y en el JSON de `MetamorfoRender`.
  - ~~**G4:** test que mida la latencia~~ → hecho (`tests/unit/LatencyTests.cpp`).
  - **G5 (Fase 4):** MIDI con precisión de muestra (ver arriba).
  - **G6 (Fases 2 a 5):** volver a medir la CPU con el motor real y añadir al CI un benchmark con límite (< 2 %, buffer 128).
- **CI:** `ilammy/msvc-dev-cmd@v1` sigue en Node 20 (no hay versión para Node 24). Si GitHub retira Node 20, reemplazarlo por `Launch-VsDevShell.ps1`.
- **CI:** añadir ccache o sccache si el tiempo de build crece.
- **Dependencia:** clap-juce-extensions dispara en Debug su propia aserción `FIXME: Handle bypass and deactivated states` (I1, inocua en Release). Revisarla cuando se actualice el commit fijado.
- **Prueba en FL:** la parte automatizable ya corre en el CI (host probes VST3 y CLAP). Para aceptar la Fase 1 solo falta la "Prueba rápida" de [TESTING_FL.md](TESTING_FL.md).
- **CI:** la slice x86_64 de macOS solo se verifica con `lipo`, porque el runner es arm64. Probarla en un Mac Intel o con Rosetta cuando sea posible.

## Fuera de alcance

- AAX (requiere SDK y firma de Avid) y VST2 (SDK descontinuado). Fuera de alcance según el SPEC §2.
- Notarización de Apple: requiere una cuenta de Apple Developer de pago. Por ahora se usa firma ad-hoc más `xattr`.
- Linux: no es objetivo. El código compila en Linux y se usa como chequeo local, pero no se distribuye.

## Excepciones de validación (con justificación)

| Validador | Prueba | Resultado | Motivo |
|---|---|---|---|
| clap-validator 0.3.2 | `preset-discovery-*` (3) | Omitidas por el validador | El plugin no implementa la fábrica de preset-discovery de CLAP. Se reevaluará en la Fase 6. |
| pluginval 1.0.4 | Formato CLAP | No aplica | pluginval no carga CLAP ("No types found"). El validador oficial del CLAP es clap-validator. |

No hay ninguna otra excepción. pluginval (nivel 10, VST3 y AU), auval y el validador VST3 de Steinberg (47/47) pasan completos.

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
| Auditoría | Parámetros con pasos decimales exactos (`steppedRange`) | En arm64, el ajuste por pasos de JUCE dejaba residuos (0 → 1.79e-7). Es el bug B2. |
| Auditoría | El sanitizador también elimina los denormales | Venían desde la entrada y pasaban a la salida (B4). |
| Auditoría | Headers de CLAP marcados `SYSTEM` | Sus warnings (más de 500 en clang) tapaban los nuestros (B6). |
| Fase 2 | Detector: HP de 55 Hz → diezmado a unos 11-12 kHz → YIN (umbral 0,15) → refinamiento parabólico a la frecuencia completa → mediana de 5 en cents + histéresis de octava (3 confirmaciones) | Error menor de 5 ¢ de 60 a 1200 Hz a 44,1, 48 y 96 kHz, a bajo costo (0,42 % de CPU) |
| Fase 2 | Voicing: aperiodicidad < 0,20 / > 0,35, RMS > −50 / < −56 dBFS y ZCR < 0,20 / > 0,30, con histéresis | Ruido y sibilantes no sonoros, silencio no sonoro, onset sonoro en menos de 60 ms |
| Fase 2 | Ventana de 2 periodos de 60 Hz y hop de 128 muestras a 48 kHz | Esa ventana fija la latencia del motor (Fase 3) |
| 2026-11-24 | Host probe de binarios (VST3 vía JUCE, CLAP con un host propio), tests de comportamiento de host, sanitizers (ASan, UBSan, TSan) en el CI | Verifica sin FL todo lo automatizable, incluida la entrada MIDI de los dos formatos. |
| Auditoría | Harness: `MetamorfoRender` (C++) + `tools/analysis` (Python con numpy y scipy) | Mide f0, formante, armónicos, latencia, clicks, robustez, alocaciones y CPU sobre el procesador real. |
| Fase 1 | Artefactos de macOS en zip creados con `ditto` | `upload-artifact` no conserva los permisos ni la estructura de los bundles. |
