# SPEC — "Metamorfo": manipulador monofónico de tono y formantes vocales

> Especificación para Claude Code. Guárdala en el repo como `docs/SPEC.md`.
> "Metamorfo" es el nombre de trabajo; se puede cambiar.
> Referencia conceptual: los manipuladores vocales compactos de una sola fila (pitch + formante independientes, modos transponer / cuantizar / robot, saturación de válvula y mezcla). Es una **implementación original**: código, nombres, sonido, interfaz y presets propios.

---

## 0. Cómo debes trabajar (Claude Code)

1. Este documento es la fuente de verdad. Trabaja **una fase a la vez** (sección 10) y no empieces la siguiente sin mi confirmación.
2. **Siempre empieza en modo plan.** Lee este archivo y `CLAUDE.md`, explora el repo, investiga lo necesario y presenta un plan con archivos, decisiones técnicas, alternativas descartadas y riesgos. No escribas código hasta que yo apruebe el plan.
3. Al terminar cada fase:
   - compila en Release y corre los tests
   - actualiza `CLAUDE.md` y `docs/ROADMAP.md`
   - haz commit con el formato `phase N: ...`
   - dame un resumen en español, con **cómo probarlo en FL Studio paso a paso**
4. `CLAUDE.md`: menos de 150 líneas (comandos, reglas inquebrantables, decisiones). No copies este SPEC; enlázalo.
5. Código y comentarios en inglés. Textos para mí y guía de usuario en español.
6. Si algo es inviable o hay una alternativa claramente mejor, dímelo antes de implementarlo.
7. Lo pendiente va a `docs/ROADMAP.md`, sin TODOs escondidos.
8. "Hecho" = compila en Windows y macOS (vía CI), pasa los tests y pasa pluginval.

**Entorno del dueño:** Windows 11, proyecto en `C:\Users\alemo\Desktop\metamorfo`. FL Studio es el DAW principal. La versión de Mac se compila y valida en el CI.

---

## 1. Reglas legales y de originalidad (obligatorias)

- **No** uses nombres ni marcas de terceros en el código, la UI, los presets o la documentación de usuario: "Little AlterBoy", "AlterBoy", "Soundtoys", "PurePitch", "Decapitator", etc.
- **No** imites la apariencia del producto de referencia. La organización funcional de una fila es libre (pitch/formante con link a la izquierda, modo al centro, drive/mix a la derecha), pero el diseño visual es propio (sección 8). Evita:
  - el logotipo retro
  - el chasis gris pizarra con la misma serigrafía
  - las mismas perillas acanaladas
  - el mismo selector de modos
- **No** hagas ingeniería inversa de binarios ni copies presets ajenos. Usa DSP de literatura pública y librerías con licencia permisiva (MIT, BSD, Apache).
- Mantén `THIRD_PARTY_LICENSES.md` actualizado.

---

## 2. Stack técnico

| Elemento | Decisión |
|---|---|
| Lenguaje | C++20 |
| Framework | **JUCE 9** (9.0.6, versión fijada). *Cambiado desde JUCE 8 en la Fase 1 por decisión del dueño: la rama 8 solo recibe parches de seguridad.* |
| Build | **CMake** + CPM.cmake o submódulos; sin Projucer |
| Formatos | **VST3** (Win/Mac), **AU** (Mac), **CLAP** (Win/Mac, vía `free-audio/clap-juce-extensions`), **Standalone** |
| AAX / VST2 | Fuera de alcance; anotarlo en el ROADMAP |
| Arquitecturas | Windows x64; macOS Universal Binary (arm64 + x86_64) |
| Canales | Mono→Mono, Mono→Estéreo, Estéreo→Estéreo (en estéreo se analiza la suma y se aplica el mismo procesamiento a ambos canales) |
| MIDI | `NEEDS_MIDI_INPUT TRUE`, `IS_SYNTH FALSE`. AU como `kAudioUnitType_MusicEffect` ("aumf"); verifica que carga como efecto. |
| Tests | Catch2 v3, `pluginval` (strictness 10), `auval`, validador VST3, `clap-validator` |
| CI | GitHub Actions: `windows-latest` + `macos-latest`, con artefactos por formato |

---

## 3. Prioridad de DAWs

1. **FL Studio (Windows y macOS)**: todo al 100 %, incluido el control por MIDI. En FL, el MIDI llega al efecto con un canal **MIDI Out** del Channel Rack cuyo *Port* coincide con el *MIDI input port* del wrapper del plugin. Verifícalo en VST3 y CLAP y documéntalo.
2. Ableton, Logic, Reaper, Bitwig, Studio One y Cubase: carga, audio, presets y automatización garantizados. El control MIDI es *best effort* y se documenta por DAW en `docs/DAW_MATRIX.md`.
3. Si una función choca con otro DAW, se implementa completa para FL y se degrada con elegancia en el otro (`juce::PluginHostType`). Nunca se rompe la carga ni el audio.

---

## 4. Cadena de señal

```
Entrada (mono limpia) ─► Filtro de análisis (HP ~60 Hz) ─► Detector de tono (f0, confianza, voiced/unvoiced)
        ─► Motor de tono + formantes (Transponer / Cuantizar / Robot; Link)
        ─► Saturación de válvula (Drive)
        ─► Mix dry/wet (dry alineado por latencia) ─► Salida
```

**Reglas de tiempo real (inquebrantables):**
- En `processBlock` no se permite: alocar memoria, locks, I/O, logs ni excepciones. Todo se reserva en `prepareToPlay`.
- `juce::ScopedNoDenormals`; salida sanitizada (sin NaN ni Inf).
- Sin estado estático mutable (FL procesa en paralelo y con varias instancias).
- Parámetros suavizados: sin zipper noise.

### 4.1 Detector de tono
- YIN con la función de diferencia calculada vía FFT, más interpolación parabólica. Evalúa MPM (McLeod) como validador contra errores de octava y elige por benchmark.
- Rango de f0: 60–1200 Hz. Hop pequeño (≤ 128 muestras a 48 kHz).
- Mediana móvil corta y histéresis anti-errores de octava.
- **Voiced / unvoiced**: aperiodicidad + energía + tasa de cruces por cero. En sibilantes y respiraciones no se aplica corrección de tono; los formantes se mantienen coherentes. Fundidos suaves en las transiciones.

### 4.2 Motor de tono y formantes
- **Método base**: TD-PSOLA con marcas de pitch sincronizadas con f0 (preserva formantes de forma natural). Formante independiente: re-muestreo de los granos (o envolvente espectral cepstral si da mejor calidad; decídelo en el plan con pruebas A/B).
- **PITCH**: de −12.0 a +12.0 semitonos, con resolución de 0.1 y 0 al centro.
- **FORMANT**: de −12.0 a +12.0, con 0 al centro.
  - Positivo: más claro y "femenino", hasta *chipmunk*.
  - Negativo: más grueso y "masculino".
  - Los ajustes sutiles de −1 a −3 deben sonar naturales: es el uso más común para dar cuerpo a coros.
- **LINK**:
  - ON: formante sigue a pitch en la misma proporción. Es un cambio tipo cinta o *DJ pitch*; puede implementarse como re-muestreo con compensación de tiempo y suele sonar más suave en material difícil.
  - OFF: pitch y formante son independientes.
  - Al activar Link, la perilla de formante refleja visualmente el valor enlazado.
- **MODO** (3 posiciones):
  - **TRANSPONER**: desplaza la voz según PITCH, sin autocorrección.
  - **CUANTIZAR**: lleva cada nota al **semitono cromático más cercano** a máxima velocidad (*hard-tune* clásico), y después aplica PITCH como transposición. Con histéresis entre notas para evitar parpadeo.
  - **ROBOT**: fija toda la voz en **una sola nota**. Con PITCH = 0, la nota es la **octava sobre el Do central (MIDI 72)**. PITCH la mueve cromáticamente en pasos enteros de semitono.
- **Control por MIDI**:
  - Transponer y Cuantizar: la nota MIDI sostenida define la transposición relativa a una nota de referencia configurable (defecto: Do central, MIDI 60). Así se tocan intervalos y armonías con el teclado.
  - Robot: la nota MIDI es la nota objetivo exacta; tocar una melodía la "canta" con la voz.
  - Sin notas activas, se regresa al valor de la perilla PITCH.
  - Portamento opcional (0–200 ms) entre notas MIDI.
  - Last-note priority.

### 4.3 Saturación (DRIVE)
- De 0 a 12. En 0 es transparente.
- Modelo propio de etapa de **válvula tipo tríodo**: predominio de armónicos pares, polarización dependiente de la envolvente, pre-énfasis y de-énfasis suaves.
- Compensación de nivel automática para que al subir Drive se escuche más carácter, no solo más volumen (dentro de ±1.5 dB).
- Sobremuestreo 2× IIR dentro de esta etapa (latencia mínima, incluida en la latencia reportada).

### 4.4 MIX
- De 0 % (DRY) a 100 % (WET). La señal dry se **retrasa exactamente la latencia del motor** para que la mezcla paralela (armonías, octavas, doblajes) quede en fase y a tiempo.

### 4.5 Latencia
- La ventana de análisis requiere latencia. **Objetivo: ≤ 30 ms** a 44.1 y 48 kHz (la referencia ronda los 50 ms). Si la calidad exige más, propón el valor en el plan, con un máximo de 60 ms.
- **Latencia fija** (no cambia con los parámetros) y siempre reportada con `setLatencySamples()`, para que la compensación de retardo de FL Studio funcione.
- Documentar que el plugin es para mezcla y producción, no para monitoreo en vivo sin latencia.

### 4.6 Rendimiento
- Una instancia a 48 kHz: menos del 2 % de un núcleo de gama media. Objetivo: 10+ instancias (voces y coros) sin problema.
- Soporta 44.1–192 kHz, buffers de 16–4096 de tamaño variable (FL Studio) y cambios de sample rate en caliente.

---

## 5. Parámetros del host (automatizables, IDs estables y versionados)

| Parámetro | Rango |
|---|---|
| `pitch` | −12…+12 st |
| `formant` | −12…+12 |
| `link` | on/off |
| `mode` | Transponer / Cuantizar / Robot |
| `drive` | 0–12 |
| `mix` | 0–100 % |
| `midiReference` | Ajustes |
| `portamento` | Ajustes |

---

## 6. Presets y utilidades

- **Barra de presets**: anterior/siguiente, menú por categorías, guardar, comparación A/B, escala de UI (75–200 %, defecto 150 %, recordada entre sesiones) y menú de opciones.
  - Los presets de usuario van en `Documents/Metamorfo/Presets/`.
  - El estado completo se guarda en el proyecto; compatible con los presets nativos de FL.
- **Presets de fábrica propios** (al menos 16):
  - Coros: "Coros con cuerpo (−2 formante)", "Doble octava abajo", "Octava arriba aireada", "Tercera para armonía"
  - Hard-tune: "Hard-tune urbano", "Hard-tune con grit", "Melódico trap"
  - Robot: "Robot monótono", "Vocoder por MIDI"
  - Personajes: "Ardilla", "Monstruo", "Voz de demonio", "Voz chiquita"
  - Color: "Calidez sutil (drive)", "Voz rota", "Radio distorsionado"
- **Utilidades de las perillas**:
  - doble clic regresa al valor por defecto
  - clic en la etiqueta alterna entre el nombre y el valor numérico exacto
  - Shift + arrastrar (Windows) o Cmd + arrastrar (macOS) da ajuste fino
  - clic en las marcas de la escala salta directo a ese valor
  - rueda del mouse ajusta
  - clic derecho → "Escribir valor"
- **Bloqueo de parámetros**: clic derecho en la etiqueta → "Bloquear". La etiqueta cambia a color de alerta y el parámetro no cambia al cargar presets. Los bloqueos se guardan en el estado.
- Pantalla de ayuda **"Control por MIDI en mi DAW"**, con los pasos de FL Studio primero (canal MIDI Out + port coincidente en el wrapper).

---

## 7. Tests

- **Unit tests (Catch2)**:
  - Detector: senos de 80–1000 Hz con error < 5 cents.
  - Transponer: +7 st sobre un seno da el resultado esperado con ±5 cents de error.
  - Formante: con pitch = 0 y formante ±6, la f0 no cambia (±5 cents) y el centroide espectral se mueve en la dirección correcta.
  - Link: pitch y formante se desplazan juntos.
  - Cuantizar: entrada desafinada +35 cents → salida < 5 cents de la nota.
  - Robot: f0 de salida = MIDI 72 con PITCH 0, y = la nota MIDI recibida.
  - Mix 0 % = null perfecto (con la latencia compensada).
  - Latencia reportada = medida.
  - Transiciones voiced/unvoiced sin clicks.
  - Sin NaN ni Inf.
  - Serialización y bloqueos.
- **Harness de render** (`tests/render`): WAV de voz → WAV por preset, para escucha A/B. Incluye un generador de "voz sintética" (pulso glotal + filtros de formantes) para los tests automáticos.
- **`docs/TESTING_FL.md`** (checklist manual):
  - insert en una pista de voz
  - MIDI Out + port y piano roll (los 3 modos)
  - automatización de Mode y Pitch
  - compensación de latencia (alinear con la voz original)
  - export vs reproducción
  - presets
  - escala de UI
  - Windows y macOS
- **macOS**: firma ad-hoc en el build y documentación de `xattr -dr com.apple.quarantine` para los artefactos del CI.

---

## 8. Interfaz de usuario (diseño original, misma lógica funcional)

**Filosofía**: todo visible al instante, sin cajones ocultos, sin espectros ni formas de onda. Ventana horizontal compacta de **una sola fila**, proporción aproximada 3:1.

**Distribución funcional** (de izquierda a derecha):
1. **Barra superior**: navegador de presets, A/B, escala de UI y opciones.
2. **Bloque izquierdo, Tono y Formante**:
   - perillas **PITCH** y **FORMANT** del mismo tamaño, con escala simétrica de −12 a +12 y el 0 arriba al centro
   - entre ambas, el botón **LINK** con LED de estado y una línea que une visualmente las dos perillas cuando está activo
3. **Bloque central, Identidad y Modo**:
   - el logotipo propio de Metamorfo
   - selector de 3 posiciones (**TRANSPONER**, **CUANTIZAR**, **ROBOT**) con LED por modo
   - indicador pequeño de la nota detectada y la nota de salida (texto discreto, como en un display de hardware)
4. **Bloque derecho, Color y Mezcla**: perillas **DRIVE** (de 0 a 12) y **MIX** (DRY ↔ WET).

**Estética propia** (no imitar el producto de referencia):
- Concepto: **"laboratorio de mutación retro-futurista"**.
- Panel de **verde oliva militar oscuro** con textura de pintura en polvo (no gris pizarra) y tornillos en las esquinas.
- Serigrafía en **blanco hueso** con tipografía condensada técnica embebida (licencia OFL).
- LEDs y estados activos en **verde fósforo / cian**; rojo solo para los bloqueos.
- Perillas propias de **cuerpo cilíndrico liso color crema** con tapa negra y una línea indicadora negra, sin acanalado.
- Selector de modo como **interruptor de palanca de 3 posiciones** (toggle de avión) con las etiquetas grabadas. No usar botones.
- Logotipo propio: la palabra "METAMORFO" en tipografía de rótulo técnico, con un pequeño ícono de onda que se transforma.
- Redimensionable de 75 a 200 % y nítida en HiDPI. Probar el escalado DPI en FL Studio en Windows.
- Tooltips en todos los controles. La UI lee el estado por atómicos; cerrar la ventana no afecta el audio.

---

## 9. Organización del repositorio

```
/CMakeLists.txt  /cmake/  /libs/
/src/
  /dsp/     PitchDetector  VoicingDetector  PsolaEngine  FormantShifter  LinkedResampler
            ModeController(Transpose/Quantize/Robot)  MidiPitchControl  TubeDrive  DryWetMix
  /plugin/  PluginProcessor  Parameters  StateSerializer  PresetManager  ParameterLocks  HostQuirks
  /ui/      PluginEditor  LookAndFeel  /components/(Knob, LedButton, ToggleLever3, NoteDisplay, PresetBar, MidiHelpScreen)
/resources/  /tests/unit/  /tests/render/  /tools/
/docs/  SPEC.md  ROADMAP.md  GUIA_USUARIO_es.md  TESTING_FL.md  DAW_MATRIX.md
/.github/workflows/build.yml
```

---

## 10. Fases y criterios de aceptación

| Fase | Entregable | Criterio de aceptación |
|---|---|---|
| **1. Esqueleto + MIDI** | CMake + JUCE + CLAP, passthrough mono/estéreo con entrada MIDI, CI Win + Mac universal con pluginval, `CLAUDE.md`, README. UI mínima que muestra la última nota MIDI recibida. | En FL Studio (Win y Mac) carga en una pista y recibe notas desde un canal MIDI Out con port coincidente (VST3 y CLAP). CI en verde. |
| **2. Detector** | PitchDetector + voicing + tests + display de nota detectada. | Tests de f0 en verde; el display sigue una voz real de forma estable. |
| **3. Motor de tono y formantes** | PSOLA, formante independiente, Link, modo Transponer, Mix con alineación, latencia fija reportada. | Tests de pitch, formante y latencia en verde; ±12 st suena utilizable en voz; los ajustes sutiles de formante suenan naturales. |
| **4. Cuantizar, Robot y MIDI** | Modos Cuantizar y Robot, control por MIDI en los 3 modos, portamento. | Hard-tune audible e inmediato; Robot canta la melodía del piano roll de FL. |
| **5. Drive** | Saturación de tríodo con compensación de nivel. | Test de armónicos (H2 > H3) y de nivel; aprobación de oído. |
| **6. UI final + presets** | Diseño de la sección 8, presets de fábrica, utilidades, bloqueos, ayuda MIDI. | Revisión visual aprobada por mí; todo persiste al reabrir el `.flp`. |
| **7. Multi-DAW + distribución** | `DAW_MATRIX.md` con resultados reales; instaladores de Windows (Inno Setup → `C:\Program Files\Common Files\VST3` y `\CLAP`) y macOS (.pkg con firma ad-hoc); guía de usuario en español. | Instalación limpia; FL lo detecta tras escanear; el resto de los DAWs verificados según la sección 3. |

---

## 11. Primera tarea

En **modo plan**, empieza por la **Fase 1**:
1. Plan de CMake (JUCE, clap-juce-extensions, entrada MIDI en un efecto, layouts mono/estéreo, pluginval) y del workflow de CI (Win + Mac universal).
2. Comprueba qué tengo instalado en Windows (Visual Studio 2022, CMake, Ninja, Git, gh) y dime qué falta.
3. Adelanta los riesgos técnicos de todo el proyecto: calidad de PSOLA en ±12 st, la latencia objetivo, la recepción MIDI en FL Studio y el AU "aumf".
4. Espera mi aprobación antes de escribir código.
