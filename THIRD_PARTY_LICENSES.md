# Licencias de terceros

Metamorfo se distribuye bajo **GNU AGPLv3** ([LICENSE.md](LICENSE.md)). Todo componente de terceros debe tener una licencia compatible y aparecer en este archivo. Las versiones exactas están en `cmake/Dependencies.cmake` y `cmake/GetCPM.cmake`.

## Incluidos en los binarios distribuidos

| Componente | Versión | Licencia | Uso |
|---|---|---|---|
| [JUCE](https://github.com/juce-framework/JUCE) | 9.0.6 | AGPLv3 (doble licencia: AGPLv3 / comercial) | Framework de audio, plugins y UI |
| [clap-juce-extensions](https://github.com/free-audio/clap-juce-extensions) | commit `3837f4a` | MIT | Wrapper CLAP para JUCE |
| [CLAP](https://github.com/free-audio/clap) | 1.2.10 (submódulo de clap-juce-extensions) | MIT | API del formato CLAP |
| [clap-helpers](https://github.com/free-audio/clap-helpers) | submódulo de clap-juce-extensions | MIT | Utilidades CLAP |
| [Barlow Condensed](https://github.com/jpt/barlow) (SemiBold, Medium) | 2017 | SIL OFL 1.1 (`resources/fonts/OFL-BarlowCondensed.txt`) | Serigrafía de la UI (embebida) |
| [Share Tech Mono](https://fonts.google.com/specimen/Share+Tech+Mono) | 2012 | SIL OFL 1.1 (`resources/fonts/OFL-ShareTechMono.txt`), nombre reservado "Share" (la fuente se usa sin modificar) | Display de notas (embebida) |
| VST3 SDK (incluido en JUCE) | el que trae JUCE 9.0.6 | MIT (VST 3 SDK 3.8+) | Formato VST3 |

## Solo para compilar y probar (no se distribuyen)

| Componente | Versión | Licencia | Uso |
|---|---|---|---|
| [CPM.cmake](https://github.com/cpm-cmake/CPM.cmake) | 0.43.2 | MIT | Gestión de dependencias en CMake |
| [Catch2](https://github.com/catchorg/Catch2) | v3.9.1 | BSL-1.0 | Tests unitarios |
| [VST 3 SDK](https://github.com/steinbergmedia/vst3sdk) | v3.8.0_build_66 | MIT | Validador VST3 en el CI |
| [pluginval](https://github.com/Tracktion/pluginval) | v1.0.4 | GPLv3 | Validación de plugins en el CI (binario oficial) |
| [clap-validator](https://github.com/free-audio/clap-validator) | 0.3.2 | MIT | Validación CLAP en el CI (binario oficial) |

## Notas

- "VST" es una marca de Steinberg Media Technologies GmbH. "Audio Units" es una marca de Apple Inc. Ambas se usan solo para nombrar los formatos.
