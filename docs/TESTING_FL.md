# Pruebas manuales en FL Studio

Checklist de verificación manual. Cada fase agrega su sección. La lista completa del SPEC (§7) se va cubriendo fase por fase.

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
| Reabrir `.flp` | | | | |
