# Matriz de compatibilidad por DAW

Estado de Metamorfo en cada DAW según el SPEC §3: **FL Studio al 100 %**; en el resto, carga, audio, presets y automatización garantizados, y control MIDI *best effort*.

**Cómo leer esta matriz.**
- "✔ CI" significa verificado automáticamente en el CI, en Windows y en macOS, sobre los **mismos binarios que se distribuyen**.
- "⏳" significa que falta la prueba manual en ese DAW. Nadie ha abierto todavía Metamorfo en esos DAWs, así que no se marca nada que no se haya comprobado.

## Verificación automática (CI, en cada push)

| Verificación | VST3 | CLAP | AU | Qué demuestra |
|---|---|---|---|---|
| pluginval, nivel 10 | ✔ CI | — (no soporta CLAP) | ✔ CI (macOS) | Carga, parámetros, estado, fuzzing, buses, editor, thread safety |
| Validador VST3 de Steinberg (3.8) | ✔ CI (47/47) | — | — | Cumplimiento del estándar VST3 |
| clap-validator 0.3.2 | — | ✔ CI (0 fallos) | — | Cumplimiento del estándar CLAP |
| auval `aumf Mmf1 Mtmf` | — | — | ✔ CI | Validación de Apple (la que usa Logic) |
| `MetamorfoHostProbe` | ✔ CI: efecto, **bus de entrada MIDI**, layouts, audio, estado | ✔ CI: **puerto de notas MIDI**, latencia, audio con notas, estado | — | Lo que un DAW necesita para enrutar MIDI hacia el efecto |
| Instalador (Fase 7) | ✔ CI: instalación silenciosa → probe del binario **instalado** → desinstalación limpia | ✔ CI: ídem | ✔ CI (macOS .pkg): instalación + auval desde `/Library` | Instalación limpia en las rutas estándar |

## Por DAW (pruebas manuales)

| DAW | Formatos | Carga como efecto | Audio y latencia | Presets y estado | Automatización | Control por MIDI | Cómo llevar MIDI al efecto |
|---|---|---|---|---|---|---|---|
| **FL Studio 2024+** (Win/Mac) | VST3, CLAP | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ prioridad 1 | Canal **MIDI Out** con *Port* = *MIDI input port* del wrapper (Settings) |
| Ableton Live 12 | VST3 (Win/Mac), AU (Mac) | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ best effort | Pista MIDI → *MIDI To*: pista de la voz → Metamorfo |
| Logic Pro | AU (`aumf`) | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ best effort | Audio FX → menú **Side Chain MIDI** del plugin |
| Reaper | VST3, CLAP, AU | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ best effort | Envío MIDI (Send) desde una pista MIDI a la pista de voz |
| Bitwig Studio | CLAP, VST3 | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ best effort | Puerto de notas CLAP / Note Receiver |
| Studio One | VST3, AU | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ best effort | Salida de una pista de instrumento hacia Metamorfo, si el host lo permite |
| Cubase / Nuendo | VST3 | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ best effort | Pista MIDI con la salida dirigida a Metamorfo (bus de eventos VST3) |

## Comportamiento con hosts sin MIDI hacia efectos

Si un DAW no puede enviar MIDI a un efecto, Metamorfo funciona igual con las perillas: PITCH, Cuantizar y Robot con Do5 + PITCH. No hay ninguna dependencia que rompa la carga ni el audio (SPEC §3).

## Cómo registrar un resultado

1. Prueba con el checklist de [TESTING_FL.md](TESTING_FL.md), adaptado a ese DAW.
2. Reemplaza el ⏳ por ✔ o ✘, con la versión del DAW y del sistema operativo.
3. Anota cualquier peculiaridad en [ROADMAP.md](ROADMAP.md).
