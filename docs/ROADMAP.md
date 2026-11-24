# ROADMAP — Metamorfo

La especificación completa está en [SPEC.md](SPEC.md). Este archivo lleva el estado de cada fase, lo que queda pendiente y las decisiones tomadas.

## Fases

| Fase | Estado | Notas |
|---|---|---|
| 1. Esqueleto + MIDI | ✅ Implementada · ⏳ falta tu prueba en FL | CMake + JUCE 9 + CLAP, passthrough, MIDI, CI, docs |
| 2. Detector de tono | ✅ Implementada (2026-11-24) · ⏳ falta tu prueba en FL | YIN diezmado + refinamiento a la frecuencia completa, voicing con 3 indicadores, display ENTRADA/SALIDA |
| 3. Motor de tono y formantes | ✅ Implementada (2026-11-24) · ⏳ falta tu escucha en FL | TD-PSOLA, formante por re-muestreo de granos, Link, Transponer, Mix alineado, latencia fija de 33,3 ms |
| 4. Cuantizar, Robot y MIDI | ✅ Implementada (2026-11-24) · ⏳ falta tu prueba en FL | Cuantizar (hard-tune con histéresis), Robot (MIDI 72 o nota MIDI), MIDI en los 3 modos con precisión de muestra, portamento de 0 a 200 ms |
| 5. Drive | ✅ Implementada (2026-11-24) · ⏳ falta tu escucha | Tríodo asimétrico con polarización dinámica, 2× IIR halfband, compensación de nivel adaptativa |
| 6. UI final + presets | ✅ Implementada (2026-11-24) · ⏳ falta tu revisión visual | Diseño del §8 (capturas en `docs/images/`), 17 presets de fábrica, presets de usuario, A/B, bloqueos, escala del 75 al 200 %, ayuda MIDI |
| 7. Multi-DAW + distribución | ✅ Implementada (2026-11-24) · ⏳ falta la prueba en los DAWs | `DAW_MATRIX.md` (verificación automática; pruebas manuales por DAW pendientes), instalador Inno Setup (Windows) y `.pkg` (macOS) **probados en el CI** con instalación limpia + desinstalación, guía de usuario en español |

## Pendiente para aceptar la Fase 1

- [ ] Prueba manual en FL Studio en Windows con VST3 y CLAP ([TESTING_FL.md](TESTING_FL.md), sección Fase 1).
- [ ] Prueba manual en FL Studio en macOS, si tienes Mac. Si no, queda pendiente hasta tener acceso a uno.
- [ ] CI en verde en `windows-latest` y `macos-latest`. Lo verifico en cada push.
- [ ] Renombrar el repositorio de GitHub a `metamorfo` (lo haces tú: Settings → Repository name). Es un requisito legal del §1.

## Pendiente para fases posteriores (identificado en la Fase 1)

- ~~**Fase 2:** f0 mínima frente a latencia~~ → decidido en automático (ver Decisiones): f0 de 60 a 1200 Hz y latencia del motor **2 × periodo máximo = 33,3 ms**.
- ~~**Fase 2:** PFFFT frente a `juce::dsp::FFT`~~ → no hace falta FFT: YIN directo sobre la señal diezmada a unos 11-12 kHz, con sumas vectorizadas, cuesta 0,42 % de un núcleo.
- **Fase 2 (pendiente menor):** MPM como validador no se implementó. YIN ya prefiere el periodo más corto por debajo del umbral, que evita los errores de sub-octava, y la mediana de 5 más la histéresis cubren los saltos de octava. Los tests con voz sintética de E2 a E5 no muestran errores de octava. Se reevaluará si aparecen en voz real.
- **Fase 3 (pendiente):** A/B de formante con envolvente cepstral. Se implementó solo el re-muestreo de granos: no añade latencia, cuesta poco y cumple las pruebas objetivas (centroide +37 % / −31 % con ±6, f0 estable en ±5 ¢, −2 sutil). La comparación cepstral queda pendiente para cuando haya escucha en voz real.
- **Fase 3 (pendiente):** plan B si ±12 st no queda utilizable en voz real (phase vocoder con preservación de envolvente). Las pruebas objetivas pasan (±12 st con 0,1 ¢); la valoración de oído está pendiente de tu prueba en FL.
- ~~**Fase 4:** MIDI con precisión de muestra~~ → hecho (cola sin locks, ver Decisiones).
- **Fase 4:** evaluar `CLAP_PROCESS_EVENTS_RESOLUTION_SAMPLES` para automatización precisa en CLAP.
- **Fase 6 (pendiente manual):** probar el escalado DPI en FL Studio en Windows. Desde JUCE 8.0.13 hay reportes de cambios en la conciencia de DPI del proceso host. La UI se dibuja en vectores con una transformación de escala, así que debería verse nítida.
- **Fase 6 (pendiente manual):** comprobar en FL que los presets nativos de FL (guardan el estado completo) y los presets propios conviven. En código, el estado del proyecto incluye parámetros, bloqueos y el nombre del preset.
- **Fase 6 (pendiente):** tu aprobación visual. Capturas: `docs/images/ui_default.png` y `docs/images/ui_link_cuantizar_bloqueo.png`.
- **Auditoría de pruebas (2026-11-23, [TEST_REPORT.md](TEST_REPORT.md)):** brechas G1 a G7 por resolver en sus fases:
  - **G1 (Fases 2 a 6):** portar cada prueba DSP del SPEC §7 a Catch2, con un generador de voz en C++. *Hecho el 2026-11-24:* el CI de macOS ya corre `run_audit.py --phase 1 --fail-on-x`. **Subir `--phase` en `build.yml` al cerrar cada fase.**
  - **G2 (Fase 3):** infraestructura de suavizado: punteros atómicos cacheados + `juce::SmoothedValue`.
  - **G3 (Fase 2):** publicar la f0 y la confianza del detector en atómicos y en el JSON de `MetamorfoRender`.
  - ~~**G4:** test que mida la latencia~~ → hecho (`tests/unit/LatencyTests.cpp`).
  - ~~**G5:** MIDI con precisión de muestra~~ → hecho.
  - **G6 (Fases 2 a 5):** volver a medir la CPU con el motor real y añadir al CI un benchmark con límite (< 2 %, buffer 128).
- **CI:** `ilammy/msvc-dev-cmd@v1` sigue en Node 20 (no hay versión para Node 24). Si GitHub retira Node 20, reemplazarlo por `Launch-VsDevShell.ps1`.
- **CI:** añadir ccache o sccache si el tiempo de build crece.
- **Dependencia:** clap-juce-extensions dispara en Debug su propia aserción `FIXME: Handle bypass and deactivated states` (I1, inocua en Release). Revisarla cuando se actualice el commit fijado.
- **Prueba en FL:** la parte automatizable ya corre en el CI (host probes VST3 y CLAP). Para aceptar la Fase 1 solo falta la "Prueba rápida" de [TESTING_FL.md](TESTING_FL.md).
- **CI:** la slice x86_64 de macOS solo se verifica con `lipo`, porque el runner es arm64. Probarla en un Mac Intel o con Rosetta cuando sea posible.

- **Depuración completa (2026-11-24, [TEST_REPORT.md](TEST_REPORT.md) §12):** B19 a B29 corregidos. Quedan como límites conocidos, con su posible mejora:
  - **Calidad en voces agudas (≥ 400 Hz) y en tono −12 + formante +12:** distancia armónica de 6 a 10 dB frente a la voz ideal (TD-PSOLA suaviza la envolvente sobre ±f0). Mejora posible: corrección cepstral de envolvente (va junto con el A/B cepstral de la Fase 3). La matriz `quality_matrix.py` tiene umbrales propios para estos casos y avisará si empeoran.
  - **Link −12 / Robot a 450 Hz:** +5,4 / +4,7 ¢ (inaudible). Revisar junto con la mejora anterior.
  - **Bypass en CLAP:** lo hace el host, porque clap-juce-extensions no marca ningún parámetro con `CLAP_PARAM_IS_BYPASS`. Al actualizar el commit fijado, comprobar si ya lo soporta (el probe CLAP lo detecta solo y entonces verifica el retardo).
  - **CPU por bloque a 192 kHz con buffers de 16:** el p99 llega al 38 % del bloque por el análisis del detector cada 512 muestras. Si algún DAW da dropouts a 192 kHz con buffers mínimos, repartir el análisis entre bloques.

## Pendiente para el lanzamiento (no automatizable aquí)

- [ ] Prueba en **FL Studio** en Windows (y macOS si es posible): "Prueba rápida" y secciones de las Fases 2 a 6 de [TESTING_FL.md](TESTING_FL.md).
- [ ] Tu **aprobación visual** de la UI (`docs/images/`).
- [ ] Escucha de calidad en voz real: ±12 st, formante sutil, hard-tune, Robot y Drive. Si ±12 no convence, se aplica el plan B (Fase 3).
- [ ] Pruebas manuales en Ableton, Logic, Reaper, Bitwig, Studio One y Cubase: completar [DAW_MATRIX.md](DAW_MATRIX.md).
- [ ] Renombrar el repositorio a `metamorfo` (requisito legal del §1).
- [ ] Versión 1.0.0 cuando todo lo anterior esté en verde.

## Fuera de alcance

- AAX (requiere SDK y firma de Avid) y VST2 (SDK descontinuado). Fuera de alcance según el SPEC §2.
- Notarización de Apple: requiere una cuenta de Apple Developer de pago. Por ahora se usa firma ad-hoc más `xattr`.
- Linux: no es objetivo. El código compila en Linux y se usa como chequeo local, pero no se distribuye.

## Excepciones de validación (con justificación)

| Validador | Prueba | Resultado | Motivo |
|---|---|---|---|
| Host probe CLAP | Bypass | Informativo, no es un fallo | El wrapper CLAP no expone parámetro de bypass; el host CLAP hace el bypass (ver límites conocidos). |
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
| Fase 7 | Instalador de Windows con Inno Setup 6 (en español; componentes VST3, CLAP y aplicación; `{commoncf64}\VST3` y `\CLAP`; desinstalador) | SPEC §10. El CI instala en silencio, carga los binarios **instalados** con el host probe y verifica la desinstalación limpia |
| Fase 7 | `.pkg` de macOS con `pkgbuild` (bundles con firma ad-hoc dentro; script postinstall que refresca la caché AU). El `.pkg` no lleva firma: los instaladores no admiten firma ad-hoc, hace falta un certificado *Developer ID Installer* | El CI lo instala con `installer` y corre el host probe y auval desde `/Library`. El usuario abre el `.pkg` con clic derecho → Abrir |
| Fase 7 | Versión 0.9.0 | Todas las funciones implementadas; 1.0.0 tras la validación manual |
| Fase 6 | UI: lienzo base de 780 × 260 escalado con una transformación (75 a 200 %, 150 % por defecto, guardado en `Metamorfo.settings` del usuario), con aspecto 3:1 fijo y redimensionable | Nítida en HiDPI (vectorial), una sola fila |
| Fase 6 | Estética propia: panel verde oliva con grano de pintura en polvo (ruido de semilla fija), tornillos, serigrafía hueso en Barlow Condensed (OFL), display de fósforo en Share Tech Mono (OFL), perillas crema lisas con tapa negra, palanca de 3 posiciones con LEDs, rojo solo para bloqueos | SPEC §8; nada imita al producto de referencia |
| Fase 6 | Presets como valores planos (`*.mtmfpreset`, XML) en `Documentos/Metamorfo/Presets/<categoría>/`; la nota de referencia MIDI no se guarda en presets (es un ajuste del estudio); los bloqueos y el nombre del preset viajan en el estado del proyecto | Bloqueos respetados al cargar presets y al alternar A/B |
| Fase 6 | Nombres de tests de Catch2 solo en ASCII | En Windows, CTest pasa el filtro con otra codificación: un "§" hacía que no se encontrara el test (fallo del CI de la Fase 5) |
| Fase 5 | Sobremuestreo 2× propio: halfband IIR polifásico de 8 secciones paso todo (diseño elíptico Valenzuela-Constantinides, banda de transición 0,04), verificado en Python: −99 dB de rechazo e imágenes por debajo de −105 dB. Latencia de 3 muestras, sumada a la reportada | SPEC §4.3: "2× IIR, latencia mínima incluida". Sin dependencias y sin JUCE en `src/dsp` |
| Fase 5 | Tríodo: pre-énfasis de +3 dB a 1,5 kHz, ganancia de 0 a +30 dB, curva asimétrica (tanh / tanh(0,45·z)/0,45) con polarización 0,2 + 0,5·envolvente (5 ms / 80 ms), de-énfasis, bloqueo de DC, compensación adaptativa de RMS (200 ms) | Búsqueda en rejilla en Python: H2 supera a H3 en al menos 15 dB de −24 a −3 dBFS. Nivel ±1,5 dB en los tests |
| Fase 5 | Drive por canal sobre la señal procesada (motor → drive → mix). Con Drive = 0, bypass bit-exacto (retardo de 3 muestras) y sin costo de CPU; al activarse entra en un fundido sobre el primer medio paso | SPEC §4: cadena de señal. Transparencia y CPU |
| Fase 5 | `tanh` racional de Lambert 7/6, acotado a ±1 | Mitad de CPU en el saturador; la diferencia es inaudible a 2× |
| Fase 4 | Cuantizar: semitono cromático más cercano con histéresis de 0,15 st más allá del punto medio y reajuste a 1 ms ("máxima velocidad"); después se aplica PITCH o el intervalo MIDI | Hard-tune inmediato sin parpadeo entre notas |
| Fase 4 | Robot: nota = 72 + round(PITCH) o la nota MIDI exacta; reajuste a 1 ms; los tramos no sonoros no se tocan | SPEC §4.2 |
| Fase 4 | MIDI: Transponer y Cuantizar usan nota − `midiReference`; sin notas vuelve a la perilla; prioridad a la última nota; portamento lineal en semitonos, de duración fija y sin glide desde el silencio | SPEC §4.2 |
| Fase 4 | Los eventos MIDI llegan al motor **retrasados maxPeriod muestras** con una cola sin locks; el display recibe las notas al instante | El horizonte de síntesis va maxPeriod por detrás de la entrada: sin el retraso, el cambio de nota sonaría 16,7 ms antes que su posición en el piano roll |
| Fase 4 | **Refinamiento síncrono de marcas**: cada marca sonora se ajusta (±P/8) por correlación cruzada normalizada con el ciclo anterior, y el ratio se calcula con el periodo real de la marca | Las marcas siguen los ciclos glóticos con vibrato y jitter: la salida de Robot y Cuantizar queda exactamente en la nota (error de −1,4 ¢ en la auditoría) |
| Fase 3 | Latencia fija = 2 × periodo máximo (60 Hz) = 33,3 ms (+3 muestras del sobremuestreo del Drive desde la Fase 5: 1603 a 48 kHz) a cualquier sample rate (1470 / 1600 / 3200 muestras a 44,1 / 48 / 96 kHz) | Decidido en automático: conserva el rango de 60 a 1200 Hz del SPEC. Supera en 3,3 ms el objetivo de 30 ms, dentro del máximo de 60. Con 70 Hz serían 28,6 ms, perdiendo bajos profundos |
| Fase 3 | TD-PSOLA: marcas de análisis a un periodo local (pseudo-periodo de 150 Hz en tramos no sonoros), marcas de síntesis a Pa/β, grano Hann de ±1 periodo **re-muestreado por el ratio de formante** y overlap-add normalizado por la suma de ventanas (piso 0,5) | Formante independiente sin latencia extra. Con Link, formante = ratio de pitch (tipo cinta) |
| Fase 3 | Ratio de pitch suavizado en log entre granos (20 ms; tramos no sonoros → 1 en 15 ms), formante suavizado 30 ms, Mix 20 ms. Límite de ratio: ×1/8 a ×8 | Sin zipper. Fundidos en las transiciones de sonoridad. Robot puede subir una voz grave hasta Do5 |
| Fase 3 | Ajustes neutros (Transponer, PITCH 0, FORMANT 0, sin MIDI) → crossfade de 20 ms a la entrada retrasada exacta, **por canal** | Transparencia bit-exacta por defecto. En estéreo no se pierde la imagen cuando no se procesa |
| Fase 3 | En estéreo, el wet es la suma mono procesada y va a ambos canales; el dry es por canal | SPEC §2: se analiza la suma y se aplica el mismo procesamiento a ambos canales |
| Fase 3 | `out = dry + mix · (procesado − dry)` | Mix 0 % y los ajustes neutros dan un null bit-exacto |
| Fase 2 | Detector: HP de 55 Hz → diezmado a unos 11-12 kHz → YIN (umbral 0,15) → refinamiento parabólico a la frecuencia completa → mediana de 5 en cents + histéresis de octava (3 confirmaciones) | Error menor de 5 ¢ de 60 a 1200 Hz a 44,1, 48 y 96 kHz, a bajo costo (0,42 % de CPU) |
| Fase 2 | Voicing: aperiodicidad < 0,20 / > 0,35, RMS > −50 / < −56 dBFS y ZCR < 0,20 / > 0,30, con histéresis | Ruido y sibilantes no sonoros, silencio no sonoro, onset sonoro en menos de 60 ms |
| Fase 2 | Ventana de 2 periodos de 60 Hz y hop de 128 muestras a 48 kHz | Esa ventana fija la latencia del motor (Fase 3) |
| 2026-11-24 | Host probe de binarios (VST3 vía JUCE, CLAP con un host propio), tests de comportamiento de host, sanitizers (ASan, UBSan, TSan) en el CI | Verifica sin FL todo lo automatizable, incluida la entrada MIDI de los dos formatos. |
| Auditoría | Harness: `MetamorfoRender` (C++) + `tools/analysis` (Python con numpy y scipy) | Mide f0, formante, armónicos, latencia, clicks, robustez, alocaciones y CPU sobre el procesador real. |
| Fase 1 | Artefactos de macOS en zip creados con `ditto` | `upload-artifact` no conserva los permisos ni la estructura de los bundles. |
