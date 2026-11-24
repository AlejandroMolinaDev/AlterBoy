# Metamorfo: guía de usuario

Metamorfo es un manipulador monofónico de voz. Cambia la **altura (pitch)** y el **timbre (formante)** por separado. Tiene tres modos (Transponer, Cuantizar y Robot), una saturación de válvula y mezcla dry/wet. Se puede controlar con MIDI.

> Metamorfo es para **mezcla y producción**. Analiza la voz con una ventana corta, lo que produce una **latencia fija de unos 33 ms**. El plugin la informa al DAW y este la compensa automáticamente. No está pensado para monitorear en vivo sin latencia.

---

## 1. Instalación

### Windows (10/11, 64 bits)

1. Ejecuta `Metamorfo-<versión>-Windows-Setup.exe`.
2. Elige los componentes. Esto es lo que instala cada uno:

   | Componente | Carpeta |
   |---|---|
   | **VST3** | `C:\Program Files\Common Files\VST3\Metamorfo.vst3` |
   | **CLAP** | `C:\Program Files\Common Files\CLAP\Metamorfo.clap` |
   | **Aplicación** | `C:\Program Files\Metamorfo\Metamorfo.exe` |

3. En FL Studio: **Options → Manage plugins → Find installed plugins**. Metamorfo aparece como **efecto** del fabricante *Metamorfo Audio*.

Para desinstalar, ve a *Configuración → Aplicaciones → Metamorfo*.

### macOS (12 o posterior, Apple Silicon e Intel)

1. Abre `Metamorfo-<versión>-macOS.pkg`. Como no está notarizado, la primera vez haz **clic derecho → Abrir → Abrir**.
2. Esto es lo que instala:

   | Formato | Carpeta |
   |---|---|
   | VST3 | `/Library/Audio/Plug-Ins/VST3` |
   | AU | `/Library/Audio/Plug-Ins/Components` |
   | CLAP | `/Library/Audio/Plug-Ins/CLAP` |
   | Aplicación | `/Applications` |

3. Si macOS bloquea el plugin ("está dañado" o "no se puede abrir"), quita la cuarentena con estos comandos en la Terminal:

```bash
sudo xattr -dr com.apple.quarantine /Library/Audio/Plug-Ins/VST3/Metamorfo.vst3
```

```bash
sudo xattr -dr com.apple.quarantine /Library/Audio/Plug-Ins/Components/Metamorfo.component
```

```bash
sudo xattr -dr com.apple.quarantine /Library/Audio/Plug-Ins/CLAP/Metamorfo.clap
```

---

## 2. Primer uso (FL Studio)

1. Envía la pista de voz a un canal del **Mixer**.
2. Carga **Metamorfo** en un slot de efectos de ese canal.
3. Con los ajustes iniciales (*preset "Inicial"*), la voz suena **idéntica** al original. Empieza a girar perillas desde ahí.

---

## 3. Controles

| Control | Qué hace |
|---|---|
| **PITCH** | Transpone de −12 a +12 semitonos, con pasos de 0,1. En **Robot** mueve la nota en semitonos enteros. |
| **FORMANT** | Cambia el timbre **sin cambiar la altura**, de −12 a +12. Negativo da una voz más gruesa; positivo, una más clara, hasta el efecto "ardilla". **Entre −1 y −3** da cuerpo natural a los coros. |
| **LINK** | El formante sigue al pitch, como al cambiar la velocidad de una cinta (*DJ pitch*). Se enciende el LED cian, una línea une las perillas y FORMANT muestra "FORMANT = PITCH". |
| **Palanca de modo** | **TRANSPONER**: desplaza la voz según PITCH. **CUANTIZAR**: lleva cada nota al semitono más cercano al instante (hard-tune) y después aplica PITCH. **ROBOT**: fija toda la voz en una nota (Do5 con PITCH en 0). |
| **DRIVE** | Saturación de válvula tipo tríodo, de 0 a 12. Añade calidez y armónicos pares. El volumen se compensa solo. En 0 es totalmente transparente. |
| **MIX** | Mezcla entre la voz original (**DRY**) y la procesada (**WET**). Las dos quedan alineadas en el tiempo, así que las armonías y octavas en paralelo suenan juntas, sin eco. |
| **Display** | **IN**: nota detectada. **OUT**: nota de salida. También muestra la nota MIDI sostenida, la nota de referencia (**REF**) y el portamento (**PORT**). |

### Trucos de las perillas

- **Doble clic**: vuelve al valor por defecto.
- **Rueda del mouse**: ajusta.
- **Shift + arrastrar** (en Mac, **Cmd + arrastrar**): ajuste fino.
- **Clic en una marca de la escala**: salta a ese valor.
- **Clic en el nombre**: alterna entre el nombre y el valor exacto.
- **Clic derecho**:
  - *Escribir valor…*
  - *Bloquear* o *Desbloquear*
  - *Valor por defecto*

### Bloqueos

Un parámetro **bloqueado** (etiqueta roja con candado) **no cambia al cargar presets** ni al alternar A/B. Sirve, por ejemplo, para conservar tu MIX mientras pruebas presets. Los bloqueos se guardan en el proyecto.

---

## 4. Barra superior

- **◀ ▶**: preset anterior y siguiente.
- **Nombre del preset**:
  - Abre el menú por categorías: Coros, Hard-tune, Robot, Personajes, Color y tus presets.
  - También permite *Guardar como…* y *Abrir la carpeta de presets*.
- **GUARDAR**: guarda tus ajustes en `Documentos/Metamorfo/Presets/<categoría>/`.
- **A / B**: alterna entre dos ajustes para compararlos. **A→B** copia el ajuste activo al otro.
- **Escala**: tamaño de la ventana, de 75 a 200 %. También puedes arrastrar la esquina inferior derecha. Metamorfo lo recuerda.
- **OPCIONES**:
  - *Control por MIDI en mi DAW*: ayuda paso a paso.
  - *Nota de referencia MIDI*.
  - *Portamento*.
  - *Carpeta de presets*.
  - *Acerca de*.

El estado completo (parámetros, bloqueos y nombre del preset) se guarda **dentro del proyecto** de tu DAW. También es compatible con los presets nativos de FL Studio.

---

## 5. Control por MIDI

| Modo | Qué hace la nota MIDI |
|---|---|
| **Transponer / Cuantizar** | La nota sostenida define el **intervalo** respecto a la **nota de referencia** (Do4 por defecto; se cambia en *Opciones*). Por ejemplo, Sol4 = +7 semitonos. Así tocas armonías con el teclado. |
| **Robot** | La nota MIDI es la **nota exacta** que canta la voz: dibuja una melodía en el piano roll y la voz la "canta". |

- Si no hay notas, Metamorfo vuelve al valor de la perilla PITCH.
- Si tocas varias notas, manda la **última**.
- **Portamento** (0 a 200 ms) desliza la voz entre notas ligadas.

### FL Studio

1. Abre la ventana de Metamorfo, ve al **engranaje** del wrapper, pestaña **Settings → MIDI** y pon **Input port** en **1**.
2. En el **Channel Rack**, agrega **(+) → MIDI Out** y ponle **Port** en **1** (el mismo número).
3. Toca o dibuja notas en el piano roll del canal **MIDI Out**.

Para otros DAWs, consulta *Opciones → Control por MIDI en mi DAW* y el archivo `docs/DAW_MATRIX.md`.

---

## 6. Presets de fábrica

| Categoría | Presets |
|---|---|
| Coros | Coros con cuerpo (−2 formante) · Doble octava abajo · Octava arriba aireada · Tercera para armonía |
| Hard-tune | Hard-tune urbano · Hard-tune con grit · Melódico trap |
| Robot | Robot monótono · Vocoder por MIDI (necesita notas MIDI) |
| Personajes | Ardilla · Monstruo · Voz de demonio · Voz chiquita |
| Color | Calidez sutil (drive) · Voz rota · Radio distorsionado |

---

## 7. Solución de problemas

| Problema | Solución |
|---|---|
| FL no muestra Metamorfo | *Manage plugins → Find installed plugins*. Revisa que la carpeta VST3 o CLAP esté en las rutas de búsqueda. |
| El MIDI no hace nada | El **Port** del MIDI Out y el **Input port** del wrapper deben tener el mismo número. Si en tu versión de FL el CLAP no recibe MIDI, usa la versión **VST3**. |
| La voz procesada suena "adelantada" o "atrasada" | Activa la compensación de latencia del DAW (en FL está activa por defecto). Metamorfo informa 33,4 ms. |
| Sonidos raros en respiraciones o "s" | Es normal que no se corrijan: Metamorfo solo cambia el tono en las partes con nota (sonoras). |
| Robot o Cuantizar no siguen una voz muy grave | El rango de detección es de 60 a 1200 Hz. |
| En Mac "no se puede abrir" | Quita la cuarentena con `xattr` (ver Instalación). |

---

Metamorfo Audio · Licencia AGPLv3 · Fuentes Barlow Condensed y Share Tech Mono (SIL OFL). Licencias de terceros en `THIRD_PARTY_LICENSES.md`.
