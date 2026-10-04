# Epicenter DSP Professional 1.0 for Windows

**Epicenter DSP Professional** es una solución de procesamiento de audio en tiempo real y reconstrucción armónica de bajos para Windows. Integra el algoritmo de procesamiento de audio directamente en el pipeline del sistema mediante un **Windows Audio Processing Object (EFX APO)**, permitiendo procesar el 100% del audio de salida (YouTube, Spotify, Apple Music, reproductores multimedia, videojuegos y navegadores) sin necesidad de cables de audio virtuales (como VB-CABLE).

---

## Arquitectura del Sistema

```text
               Windows Audio Engine (audiodg.exe)
                               │
               ┌───────────────▼───────────────┐
               │    EpicenterAPO.dll (EFX)     │
               │  - COM In-Process Server      │
               │  - Realtime Safe Audio Buffer │
               │  - Sub-ms Crossfader (A/B)    │
               └───────────────┬───────────────┘
                               │ (Direct In-Memory DSP Call)
               ┌───────────────▼───────────────┐
               │   EpicenterDSP Core (C++17)   │
               │  - Zero Allocations Path      │
               │  - Subharmonic Synthesis      │
               │  - Dynamic Bass Restoration   │
               └───────────────┬───────────────┘
                               │
                          Audio Output
                               ▲
                               │
                   Shared Memory IPC (SeqLock)
                               │
               ┌───────────────┴───────────────┐
               │  EpicenterApp.exe (WinUI 3)   │
               │  - Dark Studio Pro Interface  │
               │  - Interactive Spectrum       │
               │  - Presets & Telemetry Engine │
               └───────────────────────────────┘
```

El diseño separa estrictamente el hilo de tiempo real del procesamiento de interfaz:
- **Hilo de Audio (Real-Time Path):** Ejecuta dentro de `audiodg.exe` a través de `EpicenterAPO.dll`. Cero bloqueos (`lock-free`), cero llamadas al sistema operativo, cero asignaciones dinámicas de memoria (`no malloc / no new`) y latencia sub-milisegundo.
- **Canal IPC (Inter-Process Communication):** Memoria compartida sincronizada mediante **SeqLock** con contadores de secuencia atómicos. El APO nunca espera, nunca hace spin-lock y nunca bloquea si la UI está escribiendo.
- **Interfaz Gráfica (Control Path):** Construida en **WinUI 3 + .NET 9 LTS** con arquitectura MVVM (CommunityToolkit), paleta Dark Studio (`#0F1115`), visualizador espectral interactivo a 60 FPS, gestión de presets de estudio y panel de diagnóstico en tiempo real.

---

## Estructura del Repositorio

- **`epicenter-core/`**: Biblioteca central del algoritmo DSP en C++17 portable. Aislada con API C ABI estable, suite de pruebas unitarias y verificación de regresión acústica contra implementaciones de referencia.
- **`epicenter-apo/`**: Implementación de Windows EFX Audio Processing Object (`EpicenterAPO.dll`). Registrador COM, metadata para `audiodg.exe` y gestor de configuración.
- **`epicenter-ui/`**: Aplicación de escritorio moderna desarrollada con Windows App SDK / WinUI 3 y .NET 9 LTS.
- **`epicenter-windows/`**: Módulo host WASAPI y pruebas de aislamiento de memoria compartida.
- **`scripts/`**: Automatización PowerShell para registro/desregistro COM, configuración de endpoints en el Registro de Windows y reinicio seguro del servicio de audio.
- **`ACTIVAR_EPICENTER_APO.bat`**: Activador con elevación UAC y persistencia de consola.
- **`DESACTIVAR_EPICENTER_APO.bat`**: Restaurador del sistema de audio a su configuración original de fábrica.
- **`BUILD_ALL.bat`**: Script de compilación integral para Core, APO y UI.

---

## Parámetros del Motor DSP

| Parámetro | Rango | Descripción |
|---|---|---|
| **Intensity** | 0.0 - 1.0 | Cantidad de señal sintetizada y reconstrucción armónica inyectada en la mezcla. |
| **Frequency Sweep** | 27 Hz - 63 Hz | Frecuencia de sintonización central para la reconstrucción de graves profundos. |
| **Bandwidth** | 0.2 - 2.0 oct | Ancho de banda de captura del filtro subarmónico. |
| **Bass Boost** | 0.0 - 12.0 dB | Realce post-reconstrucción dinámico. |
| **Low Cut** | 15 Hz - 40 Hz | Filtro pasa-altos de protección para transductores contra excursión excesiva. |
| **Harmonics Mode** | Even / Odd / Mixed | Carácter tímbrico de los armónicos generados. |
| **Bypass (A/B)** | On / Off | Conmutación instantánea libre de clicks mediante crossfader logarítmico sub-milisegundo. |

---

## Requisitos de Desarrollo

- **Sistema Operativo:** Windows 10 (versión 1809 o superior) o Windows 11 (x64).
- **Compilador C++:** Visual Studio 2022 con la carga de trabajo *Desarrollo para el escritorio con C++* (MSVC v143).
- **CMake:** Versión 3.20 o superior.
- **.NET SDK:** .NET 9.0 SDK o superior.
- **Windows App SDK:** Soporte para WinUI 3 (incluido en las dependencias NuGet del proyecto).

---

## Compilación

Para compilar todo el proyecto (Core C++, Windows APO y WinUI 3 UI) en modo Release:

```cmd
BUILD_ALL.bat
```

O paso a paso de forma manual:

```cmd
:: 1. Compilar Core DSP
cmake -B epicenter-core/build -S epicenter-core -DCMAKE_BUILD_TYPE=Release
cmake --build epicenter-core/build --config Release

:: 2. Compilar Windows APO
cmake -B epicenter-apo/build -S epicenter-apo -DCMAKE_BUILD_TYPE=Release
cmake --build epicenter-apo/build --config Release

:: 3. Compilar Interfaz WinUI 3
dotnet build epicenter-ui/EpicenterUI.sln -c Release
```

---

## Uso y Pruebas en Tiempo Real

### Activar el sistema:
1. Haz doble clic en **`ACTIVAR_EPICENTER_APO.bat`** (acepta la solicitud de UAC de Windows).
2. El script vinculará automáticamente el APO a los dispositivos de salida activos y abrirá la interfaz de control.
3. Reproduce cualquier audio en YouTube, Spotify o tu reproductor habitual.
4. Ajusta los controles de **Intensity** y **Sweep** en la aplicación y comprueba la reconstrucción de graves en tiempo real.

### Desactivar y restaurar Windows:
1. Haz doble clic en **`DESACTIVAR_EPICENTER_APO.bat`**.
2. El script desvinculará el APO, restaurará los efectos de audio originales del sistema y reiniciará el servicio de audio (`audiosrv`).

---

## Licencia

Epicenter DSP Professional es propiedad de su autor. Todos los derechos reservados.
