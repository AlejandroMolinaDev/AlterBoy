# Pruebas manuales en FL Studio

Checklist de verificación manual. Cada fase agrega su sección. La lista completa del SPEC (§7) se va cubriendo fase por fase.

---

## Prueba rápida (5 minutos): solo lo que la automatización no puede cubrir

Desde el 2026-11-24, el CI comprueba en Windows y macOS que los binarios VST3 y CLAP:
- cargan en un host
- son efectos
- **declaran la entrada MIDI** (bus de eventos en VST3; puerto de notas MIDI en CLAP)
- procesan sin errores
- guardan su estado y aguantan varias instancias

Detalle en [TEST_REPORT.md](TEST_REPORT.md) §10. Lo único que falta confirmar a mano en FL:

1. **Escaneo:** *Options → Manage plugins* → agrega la carpeta de los binarios → *Find installed plugins*. Aparecen Metamorfo (VST3) y Metamorfo (CLAP) como efectos.
2. **MIDI Out → port:**
   1. Inserta Metamorfo (VST3) en un canal del Mixer.
   2. En el engranaje del wrapper, ve a *Settings* y pon *MIDI → Input port* en **1**.
   3. En el Channel Rack, agrega **MIDI Out** con *Port* en **1**.
   4. Toca notas. El display del plugin las muestra.
3. **Lo mismo con el CLAP:** repite el punto 2 con Metamorfo (CLAP).
4. **`.flp`:** guarda el proyecto, ciérralo y vuelve a abrirlo. El plugin carga.
5. **Export:** exporta a WAV. Suena igual que la reproducción (en la Fase 1, igual que la voz original).

Si los 5 puntos pasan, la Fase 1 queda aceptada. Abajo sigue el checklist completo.

---

## Fase 1: carga, audio y recepción MIDI

**Requisitos:**
- FL Studio 2024 o posterior (FL soporta CLAP desde la versión 2024).
- Los artefactos del CI (`Metamorfo-win-VST3`, `Metamorfo-win-CLAP`, o `Metamorfo-mac`) o tu build local en `build/release/Metamorfo_artefacts/Release/`.

### 1. Instalar o escanear

**Windows, sin permisos de administrador:**
1. Descomprime los artefactos en una carpeta fija, por ejemplo `C:\Users\alemo\Documents\Metamorfo-dev\`.
2. En FL: **Options → Manage plugins**.
3. En **Plugin search paths**, agrega esa carpeta (o la carpeta `build\release\Metamorfo_artefacts\Release` de tu build local).
4. Pulsa **Find installed plugins**.

**Windows, alternativa:** copia `Metamorfo.vst3` a `C:\Program Files\Common Files\VST3\` y `Metamorfo.clap` a `C:\Program Files\Common Files\CLAP\`. Ambas carpetas requieren administrador.

**macOS:**
1. Copia los bundles a `~/Library/Audio/Plug-Ins/VST3/`, `~/Library/Audio/Plug-Ins/CLAP/` y `~/Library/Audio/Plug-Ins/Components/`.
2. Quita la cuarentena de cada bundle copiado, por ejemplo:

```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Metamorfo.vst3
```

**Comprobar:** en el Plugin Manager aparecen **Metamorfo (VST3)** y **Metamorfo (CLAP)** como **efectos** (no como generadores) del fabricante **Metamorfo Audio**.

### 2. Insertarlo en una pista con voz

1. Carga una voz en el Playlist (o un sample en el Channel Rack) y envíala a un canal del Mixer, por ejemplo el **Insert 1**.
2. En un slot de efectos del Insert 1, elige **Metamorfo (VST3)**.
3. Reproduce. **Esperado:** la voz suena **idéntica** a la original, porque en la Fase 1 Metamorfo no modifica el audio.
4. Mira la línea inferior de la ventana del plugin. **Esperado:** dice `VST3 | Estéreo → Estéreo | Host: FL Studio | 44100 Hz` (o tu sample rate).

### 3. Enviar notas MIDI al efecto (la prueba clave)

1. Abre la ventana del plugin y haz clic en el **engranaje** del wrapper (arriba a la izquierda), pestaña **Settings**.
2. En **MIDI**, fija el **Input port** en **1**.
3. En el **Channel Rack**, agrega **Add (+) → MIDI Out**.
4. En la ventana de MIDI Out, fija el **Port** en **1**, el mismo número que el del paso 2. El canal puede quedar en 1.
5. Selecciona el canal MIDI Out y toca el teclado de la computadora o un teclado MIDI. **Esperado:**
   - el número grande muestra la nota (por ejemplo `C5 (72)`) y se pone **verde** mientras la mantienes
   - "Eventos" aumenta en cada nota presionada o soltada
   - "Canal" y "Velocidad" coinciden con lo que tocas
6. Abre el **Piano roll** del canal MIDI Out, dibuja una melodía y dale Play. **Esperado:** el display sigue las notas y "Sostenida" vuelve a `--` al terminar cada nota.
7. Toca dos notas a la vez y suelta la segunda. **Esperado:** "Sostenida" regresa a la primera (prioridad a la última nota).
8. Repite los pasos 2 a 7 con **Metamorfo (CLAP)**. La línea inferior debe decir `CLAP`.

Si una versión **no** recibe notas, anota cuál (VST3 o CLAP), tu versión de FL y lo que ves. Esto define si en FL se recomienda solo VST3 (SPEC §3).

### 3b. Modo Robot por MIDI (la configuración que se usará en la Fase 4)

En la Fase 1 la UI todavía no tiene el selector de modo, y Robot aún no cambia el audio. Esta prueba confirma que el parámetro se puede automatizar y que las notas llegan con Robot activo.

1. Abre la ventana del plugin. En el menú del wrapper (flecha o engranaje arriba a la izquierda), elige **Browse parameters**. FL muestra los parámetros en el Browser.
2. Busca **Mode**. Haz clic derecho sobre él y elige **Edit events** (o **Create automation clip**). Ponlo en **Robot**: es el valor más alto de los tres; con la rueda o el editor de eventos se ve el texto "Robot".
3. Verifica que el **MIDI Out** sigue en el **Port 1** y que el *MIDI input port* del wrapper también está en **1**.
4. En el **Piano roll** del canal MIDI Out, dibuja una melodía corta, por ejemplo Do4–Mi4–Sol4–Do5, con notas separadas.
5. Dale Play con la voz sonando en el Insert. **Esperado en la Fase 1:** el display sigue cada nota de la melodía, la voz suena **sin cambios** y el automation clip de Mode no produce clicks.
6. **Esperado a partir de la Fase 4:** la voz "canta" exactamente la melodía del piano roll. Sin notas, se queda en Do5 (MIDI 72), desplazado por PITCH.

### 4. Guardar, reabrir y robustez

1. Guarda el proyecto `.flp`, ciérralo y vuelve a abrirlo. **Esperado:** el plugin carga sin errores.
2. Cierra y abre la ventana del plugin mientras suena. **Esperado:** el audio no se corta.
3. Cambia el tamaño de buffer: **Options → Audio settings → Buffer length**, prueba con 256 y con 2048. **Esperado:** sin clics ni cortes.
4. Exporta el proyecto a WAV. **Esperado:** el archivo exportado suena igual que la reproducción.
5. Carga 10 instancias en distintos inserts. **Esperado:** FL sigue estable.

### 5. Standalone (opcional)

1. Abre `Metamorfo.exe` (o `Metamorfo.app`).
2. En **Options → Audio/MIDI Settings**, elige tu entrada de audio y tu teclado MIDI.
3. Toca notas. **Esperado:** el display las muestra.

### Resultado de la Fase 1

| Prueba | Windows VST3 | Windows CLAP | macOS VST3 | macOS CLAP |
|---|---|---|---|---|
| Aparece como efecto | | | | |
| Passthrough idéntico | | | | |
| Recibe MIDI (MIDI Out + port) | | | | |
| Piano roll y prioridad de nota | | | | |
| Mode = Robot automatizable, las notas llegan | | | | |
| Reabrir `.flp` | | | | |

---

## Fase 2: detector de nota

1. Inserta Metamorfo en una pista de voz y dale Play.
2. **Esperado:** la línea **ENTRADA** muestra la nota cantada con sus cents, por ejemplo `A3 +12¢`. Debe ser estable en las notas sostenidas y sin saltos de octava.
3. En respiraciones, sibilantes ("s", "sh") y silencios muestra `--`.
4. En la Fase 2, **SALIDA** coincide con ENTRADA porque todavía no se procesa el audio.

---

## Fase 3: tono, formante, Link y Mix

1. **Transponer:** en *Browse parameters*, sube **Pitch** a +7.
   - La voz sube una quinta.
   - SALIDA muestra la nota de ENTRADA + 7.
   - Prueba también ±12 y ±1.
2. **Formante:** Pitch en 0, **Formant** en −3.
   - La voz suena con más cuerpo, a la misma altura.
   - Prueba +6 (más clara) y −6 (más grave).
3. **Link:** Pitch en +5 y **Link** activado. El timbre sube junto con la altura (efecto cinta).
4. **Mix:**
   1. Duplica la pista de voz sin el plugin.
   2. Pon Mix en 50 % y Pitch en +12.
   3. ✅ La octava queda **alineada en el tiempo** con la voz original, sin eco.
5. **Compensación de latencia (PDC):**
   1. Con Mix en 0 %, invierte la fase de la copia sin plugin.
   2. ✅ Se cancelan por completo (silencio). Eso confirma que FL compensa los 33,3 ms.

---

## Fase 4: Cuantizar, Robot y MIDI

1. **Cuantizar (hard-tune):**
   1. Pon **Mode** en *Cuantizar* y canta o reproduce una voz un poco desafinada.
   2. ✅ Cada nota se "pega" al semitono más cercano de inmediato. Con vibrato se oye el efecto robótico clásico.
   3. Con Pitch en +3, el resultado queda transpuesto 3 semitonos.
2. **Robot sin MIDI:**
   1. Pon Mode en *Robot*.
   2. ✅ Toda la voz queda en **Do5** (MIDI 72).
   3. Pitch en −5 la lleva a Sol4.
3. **Robot con MIDI (canal MIDI Out + port):**
   1. Wrapper → *MIDI input port* = 1. Canal **MIDI Out** → *Port* = 1.
   2. Dibuja una melodía en el piano roll del MIDI Out.
   3. ✅ La voz "canta" la melodía, **alineada con las notas del piano roll**.
   4. Al soltar todas las notas vuelve a Do5.
4. **Transponer con MIDI:**
   1. Mode en *Transponer*, referencia en Do4.
   2. Mantén Sol4. ✅ La voz sube una quinta.
   3. Suelta. ✅ Vuelve al valor de la perilla Pitch.
5. **Portamento:** pon **Portamento** en 150 ms y toca notas ligadas en Robot. ✅ La voz se desliza entre las notas.

---

## Fase 5: Drive

1. Sube **Drive** a 3, 6 y 12 con la voz sonando.
2. ✅ Aparece calidez y saturación de válvula, más "carácter" pero **sin un salto de volumen** (la compensación lo mantiene a ±1,5 dB).
3. Con Drive en 0, el sonido es idéntico al original.
4. Con Pitch en +12 y Drive en 8, la saturación se aplica a la voz procesada.
