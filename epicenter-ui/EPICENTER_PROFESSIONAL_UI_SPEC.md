# EPICENTER DSP PROFESSIONAL 1.0 — UI ARCHITECTURE & DESIGN SPECIFICATION
**Paso 3.1 — Arquitectura de Software, Sistema de Diseño y Protocolos**

---

## 0. Correcciones Previas del Backend (Paso 2 Baseline)

### 0.1. Auditoría del SeqLock: `sequence` y Primitivas de Sincronización
Se analizó formalmente el uso de `volatile uint32_t sequence` en el protocolo IPC:
1. **Límite de `volatile` en C++ estándar**: En el estándar C++, `volatile` previene optimizaciones de registro en el hilo actual, pero no define semántica de barrera de memoria inter-núcleo (*inter-thread visibility*).
2. **Garantía bajo MSVC en Windows x86/x64**: En el compilador MSVC para Windows (arquitectura x64), los enteros de 32 bits con alineación natural (`#pragma pack(push, 8)`) son atómicos por bus de memoria. Sin embargo, para no depender de comportamientos del compilador, el protocolo se apoya en:
   - **`MemoryBarrier()` (Hardware Fence / `_mm_mfence`)**: Ejecutado antes y después de alterar los campos de control.
   - **En C# (.NET 10 LTS)**: La UI utilizará `Interlocked.Increment` y `Volatile.Read` / `Volatile.Write` sobre el puntero mapeado (`SafeMemoryMappedViewHandle`). `Interlocked.Increment` genera la instrucción de hardware `lock xadd`, la cual fuerza una invalidación inmediata en el protocolo de coherencia de caché de la CPU (MESI/MOESI) en todos los núcleos del procesador.
3. **Garantía Real-Time en el APO**:
   El callback `APOProcess()` en el hilo de audio **NUNCA hace spin (`while`), NUNCA hace wait, NUNCA invoca mutexes ni llamadas al kernel**. Realiza exactamente dos lecturas atómicas de registro; si detecta colisión o número impar, retorna `false` en 13 nanosegundos ($O(1)$ constante) y conserva los parámetros en caché.

### 0.2. Nomenclatura del Crossfade
El crossfader del APO queda formalmente catalogado como:
$$\textbf{8 ms Linear Clickless Crossfade}$$
No se utilizarán los términos *Equal Power* ni *Constant Power*, ya que su fórmula matemática es una rampa lineal de primer orden:
$$\text{dryGain} = 1.0f - \alpha, \quad \text{wetGain} = \alpha$$
Su función técnica es la eliminación de discontinuidades transitorias (*anti-transient micro-fade*), computacionalmente determinista e invariante ante la frecuencia de muestreo.

---

## 1. Arquitectura de la Aplicación (.NET 10 LTS + WinUI 3)

La aplicación sigue el patrón arquitectónico **MVVM (Model-View-ViewModel)** estricto con desacoplamiento total del procesamiento de audio:

```text
┌────────────────────────────────────────────────────────────────────────┐
│                        WINUI 3 PRESENTATION LAYER                      │
│   ┌─────────────────────┐   ┌──────────────────┐   ┌────────────────┐  │
│   │     MainWindow      │   │   SettingsView   │   │  KnobControl   │  │
│   └──────────┬──────────┘   └─────────┬────────┘   └───────┬────────┘  │
│              │ (XAML DataBinding)     │                    │           │
│              ▼                        ▼                    ▼           │
│   ┌─────────────────────┐   ┌──────────────────┐   ┌────────────────┐  │
│   │    MainViewModel    │   │SettingsViewModel │   │ MeterViewModel │  │
│   └──────────┬──────────┘   └─────────┬────────┘   └───────┬────────┘  │
└──────────────┼────────────────────────┼────────────────────┼───────────┘
               │                        │                    │
               ▼                        ▼                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│                          SERVICES & DOMAIN                             │
│   ┌───────────────────────────┐      ┌─────────────────────────────┐   │
│   │    ApoIpcBridgeService    │      │    DeviceMonitorService     │   │
│   │   (MemoryMappedFile)      │      │    (IMMDeviceNotification)  │   │
│   └─────────────┬─────────────┘      └──────────────┬──────────────┘   │
│                 │                                   │                  │
│                 ▼                                   ▼                  │
│   ┌───────────────────────────┐      ┌─────────────────────────────┐   │
│   │       PresetService       │      │   DiagnosticsCollector      │   │
│   └───────────────────────────┘      └─────────────────────────────┘   │
└────────────────────────────────────────────────────────────────────────┘
                                   │
               (Inter-Process Shared Memory - Zero Sockets/Pipes)
                                   │
                                   ▼
┌────────────────────────────────────────────────────────────────────────┐
│             WINDOWS AUDIO ENGINE (Session 0 - LOCAL SERVICE)           │
│   ┌────────────────────────────────────────────────────────────────┐   │
│   │  audiodg.exe                                                   │   │
│   │   └─ EpicenterAPO.dll (EFX)                                    │   │
│   │        ├─ Lock-Free SeqLock Consumer                           │   │
│   │        ├─ 8 ms Linear Clickless Crossfader                     │   │
│   │        └─ EpicenterDSPCore (C++ Pure Audio Engine)             │   │
│   └────────────────────────────────────────────────────────────────┘   │
└────────────────────────────────────────────────────────────────────────┘
```

### Reglas Arquitectónicas Infranqueables:
1. **C# NUNCA procesa audio**: No hay buffers de audio pasando a través de la memoria gestionada (.NET CLR).
2. **C# NUNCA bloquea el hilo de audio**: La comunicación es asíncrona mediante lectura/escritura en la página de memoria compartida de 4KB.
3. **Tasa de Refresco de UI Independiente**: La UI corre a 60 FPS mediante un despacho desacoplado (`DispatcherQueueTimer`), consumiendo la telemetría ya calculada por el APO (pico, RMS, carga de CPU).

---

## 2. Estructura de Carpetas del Proyecto

```
EpicenterProfessional/
│
├── Core/                                  # Baseline C++ Core (Paso 1 - Congelado)
│   ├── include/EpicenterDSP.h
│   └── src/
│
├── Audio/                                 # Baseline Windows APO (Paso 2 - Congelado)
│   ├── include/EpicenterApo.h
│   ├── src/EpicenterApo.cpp
│   └── inf/EpicenterAPO.inf
│
├── IPC/                                   # Definiciones de Protocolo Compartidas
│   └── EpicenterApoIpc.h
│
├── ProfessionalApp/                       # WinUI 3 en .NET 10 LTS
│   ├── App.xaml                           # Entry point y configuración del Host
│   ├── App.xaml.cs
│   │
│   ├── Assets/                            # Iconos vectoriales y branding Studio
│   │   ├── Brand/ (Logos SVG/PNG, Badges)
│   │   └── Icons/ (Bypass, Settings, AudioDevice, A/B)
│   │
│   ├── Themes/                            # Sistema de Diseño Dark Studio
│   │   ├── Colors.xaml                    # Paleta Dark Obsidian, Amber Gold, Studio Cyan
│   │   ├── Typography.xaml                # Fuentes Segoe UI Variable y Tabular Monospace
│   │   ├── Styles.xaml                    # Botones táctiles, cards, switches industriales
│   │   └── Controls/                      # Estilos de controles personalizados
│   │
│   ├── Controls/                          # Controles Visuales Especializados de Audio
│   │   ├── RotaryKnob.xaml                # Knob vectorial con dragging vertical/horizontal
│   │   ├── RotaryKnob.xaml.cs
│   │   ├── LedMeterBar.xaml               # Barra LED estéreo con peak hold y clip
│   │   ├── LedMeterBar.xaml.cs
│   │   ├── AbToggleSwitch.xaml            # Selector industrial A/B iluminado
│   │   └── StatusIndicator.xaml           # Indicador de estado del APO
│   │
│   ├── Models/                            # Estructuras de datos de dominio
│   │   ├── ApoTelemetryData.cs            # Telemetría decodificada (RMS, Peak, CPU, Freq)
│   │   ├── AudioDeviceInfo.cs             # Metadatos del endpoint de audio activo
│   │   ├── PresetItem.cs                  # Modelo de preset de usuario/fábrica
│   │   └── AppSettings.cs                 # Preferencias de la aplicación
│   │
│   ├── Services/                          # Lógica de infraestructura y SO
│   │   ├── Interfaces/
│   │   │   ├── IIpcBridgeService.cs
│   │   │   ├── IDeviceManagerService.cs
│   │   │   ├── IPresetService.cs
│   │   │   └── ISettingsService.cs
│   │   ├── ApoIpcBridgeService.cs         # Conexión nativa con MemoryMappedFile
│   │   ├── WindowsDeviceService.cs        # Enumeración y monitoreo de dispositivos WASAPI
│   │   ├── PresetService.cs               # Gestión de los 5 presets base
│   │   └── SettingsService.cs             # Almacenamiento local seguro
│   │
│   ├── ViewModels/                        # Lógica de presentación MVVM
│   │   ├── MainViewModel.cs               # Estado de la ventana principal, knob y telemetría
│   │   ├── SettingsViewModel.cs           # Configuración de dispositivos, autoinicio y diagnóstico
│   │   └── DiagnosticsViewModel.cs        # Inspector técnico de rendimiento
│   │
│   └── Views/                             # Vistas XAML
│       ├── MainWindow.xaml                # Ventana principal de procesamiento de estudio
│       ├── MainWindow.xaml.cs
│       ├── SettingsWindow.xaml            # Diálogo flotante o panel de configuración
│       └── SettingsWindow.xaml.cs
│
├── Licensing/                             # Placeholder para fase futura
│   └── ILicensingService.cs
│
├── Installer/                             # Placeholder para fase futura
│
└── Tests/                                 # Pruebas automatizadas de UI e IPC
    ├── EpicenterApp.Tests/
    │   ├── IpcBridgeTests.cs              # Concurrencia y reconexión C# -> C++
    │   ├── ViewModelTests.cs              # Máquina de estados de la UI
    │   └── PresetTests.cs                 # Validación de límites de parámetros
```

---

## 3. Componentes Principales de la UI

1. **Header Bar (Barra Superior de Control)**:
   - Logotipo e Isotipo "EPICENTER DSP PROFESSIONAL".
   - Status Badge del APO (`● ACTIVE`, `● BYPASS`, `⚠ UNAVAILABLE`).
   - Botón de Presets rápidos (Dropdown de estudio).
   - Acceso a Configuración (Icono de engranaje minimalista).
   - Controles de ventana nativos personalizados con Mica/Acrylic oscuro.
2. **Main Processing Canvas (Área Central de Procesamiento)**:
   - **Intensity Rotary Knob**: Knob circular de 140 px de diámetro con arco de LED Amber Glow progresivo, lectura numérica central tabular grande (e.g. `75%`).
   - **Controles de Conmutación Táctil**:
     - Botón Principal `MASTER ON / OFF` (retroiluminado en ámbar al activarse).
     - Selector `A / B` con micro-animación de conmutación instantánea.
3. **Metering Bay (Bahía de Medición Acústica)**:
   - Medidor estéreo `INPUT L/R` (Pre-DSP) y `OUTPUT L/R` (Post-DSP / Post-Crossfader).
   - Balística dual: Barra sólida para nivel RMS medio y aguja fina para True Peak.
   - Indicador de recorte (`CLIP`) superior que se enciende en rojo $> -0.1$ dBFS.
4. **Telemetry & Diagnostics Strip (Barra Inferior de Telemetría)**:
   - Tira técnica con tipografía monoespaciada:
     `48.0 kHz  |  24-bit  |  Stereo  |  256 spl  |  0.0 ms Latency  |  1.8% DSP CPU  |  Altavoces Realtek`

---

## 4. Flujo de Estado y Ciclo de Datos

```text
[ Usuario gira el Knob ]
          │
          ▼
[ RotaryKnob.OnDeltaPointer ] ──> Clamps [0.0, 100.0]
          │
          ▼
[ MainViewModel.Intensity ] ──(Property Change)──> [ Vista actualiza texto ]
          │
          ▼
[ ApoIpcBridgeService.UpdateControl(enabled, intensity, abState) ]
          │
          ├──> Interlocked.Increment(sequence) [Odd]
          ├──> MemoryBarrier()
          ├──> Write: enabled, intensity, abState
          ├──> MemoryBarrier()
          └──> Interlocked.Increment(sequence) [Even]
                    │
                    ▼ (Memoria Compartida de 4KB)
                    │
[ audiodg.exe: CEpicenterApo::APOProcess() lee en el siguiente bloque ]
          │
          ▼
[ APO actualiza Crossfader y Core DSP ]
          │
          ▼
[ APO escribe telemetría (QPC Time, Peak, RMS, Heartbeat) ]
          │
          ▼ (Memoria Compartida de 4KB)
          │
[ DispatcherQueueTimer a 60 FPS en UI ]
          │
          ▼
[ ApoIpcBridgeService.ReadTelemetry() ]
          │
          ▼
[ MainViewModel actualiza Meters y CPU sin bloquear ]
```

---

## 5. Diseño Conceptual de la Pantalla Principal (Main Window)

```text
┌─────────────────────────────────────────────────────────────────────────────────┐
│ [≡] EPICENTER DSP PROFESSIONAL  [Preset: Deep Power ▾]        ● ACTIVE   [⚙] [-][×]│
├─────────────────────────────────────────────────────────────────────────────────┤
│                                                                                 │
│                        SUBHARMONIC SYNTHESIZER                                  │
│                                                                                 │
│                                ╭────────╮                                       │
│                             ╭──╯   ▲    ╰──╮                                    │
│                            │    ╭─────╮     │                                   │
│                            │    │ 75  │     │                                   │
│                            │    │  %  │     │                                   │
│                            │    ╰─────╯     │                                   │
│                             ╰──╮        ╭──╯                                    │
│                                ╰────────╯                                       │
│                                INTENSITY                                        │
│                                                                                 │
│                    ┌────────────────────────────────┐                           │
│                    │    [  OFF  ]   │ ● [  ON  ]    │                           │
│                    └────────────────────────────────┘                           │
│                                                                                 │
│            ┌─────────────────┐            ┌─────────────────┐                   │
│            │  A  ORIGINAL    │            │  B  EPICENTER ● │                   │
│            └─────────────────┘            └─────────────────┘                   │
│                                                                                 │
│    INPUT L/R                                              OUTPUT L/R            │
│    L [████████████████░░░░░░░░] -12.4 dB   L [████████████████████░░░░]  -4.1 dB│
│    R [████████████████░░░░░░░░] -12.4 dB   R [████████████████████░░░░]  -4.1 dB│
│    -48  -36  -24  -18  -12  -6   0 dB      -48  -36  -24  -18  -12  -6   0 dB   │
│                                                                                 │
├─────────────────────────────────────────────────────────────────────────────────┤
│ 48.0 kHz  │  32-bit Float  │  2 Ch  │  256 frames  │  0.0 ms  │  1.2% CPU  │ Altavoces │
└─────────────────────────────────────────────────────────────────────────────────┘
```

---

## 6. Diseño Conceptual de la Pantalla de Configuración (Settings)

Diseñado como un panel lateral (*flyout drawer*) o ventana modal secundaria con navegación por pestañas:

1. **Audio Device Settings**:
   - Selector de Dispositivo de Salida Activo (Dropdown enumerando endpoints WASAPI).
   - Estado de Enlace del APO en el endpoint (`Bound / Enabled` o `Not Bound`).
   - Botón `Re-scan Devices`.
2. **Behavior & Startup**:
   - `Iniciar con Windows` (Toggle Switch).
   - `Minimizar a la bandeja del sistema (System Tray)` (Toggle Switch).
   - `Estado inicial al abrir`: Recordar último estado / Siempre encendido / Siempre apagado.
3. **Diagnostics & Troubleshooting**:
   - `APO Process ID`: Muestra PID de `audiodg.exe`.
   - `IPC Heartbeat`: Contador de bloques procesados en tiempo real.
   - `Botón "Test Audio Burst"`: Emite un tono sintetizado de 60 Hz para verificar el procesamiento.
   - `Exportar Registro de Diagnóstico`: Genera archivo `.log` con datos de endpoints y versiones.

---

## 7. Sistema de Colores (Dark Studio Palette)

La paleta evoca el hardware de masterización de gama alta (Neve, SSL, FabFilter Pro-L2):

| Rol Semántico | Nombre del Token | Valor Hex | Uso en UI |
|---|---|---|---|
| **Fondo Base** | `StudioBackgroundDark` | `#0D0E12` | Lienzo principal de la ventana |
| **Superficie Rack** | `StudioSurfaceCard` | `#14171F` | Paneles elevados, bahías de medición |
| **Borde / Bisel** | `StudioBorderSubtle` | `#232733` | Biseles finos de 1px entre secciones |
| **Acento Primario** | `EpicenterAmberGold` | `#F59E0B` | Arco del Knob, botón ON activo, armónicos |
| **Acento Resplandor** | `AmberGlowSoft` | `#4D3A14` | Sombra difusa / resplandor LED |
| **Acento Secundario** | `StudioCyanMetric` | `#06B6D4` | Medidores de audio, frecuencias, telemetría |
| **Texto Primario** | `TextPrimaryHigh` | `#F8FAFC` | Valores numéricos, nombres de controles |
| **Texto Secundario** | `TextSecondaryMuted`| `#94A3B8` | Etiquetas técnicas, unidades de medida |
| **Estado Activo** | `StatusGreenOk` | `#10B981` | Indicador de APO conectado y en ejecución |
| **Estado Alerta** | `StatusRedClip` | `#EF4444` | Indicador de saturación o clipping $> 0$ dBFS |

---

## 8. Tipografía

Jerarquía técnica basada en **Segoe UI Variable** con números monoespaciados para evitar vibración visual (*layout jitter*):

1. **Display Value (Centro del Knob)**:
   - Familia: `Cascadia Mono` / `Segoe UI Mono`
   - Tamaño: `36 pt`, Peso: `Bold`
   - Alineación: Tabular / Monospace (ancho fijo por carácter)
2. **Section Headings (Subharmonic Synthesizer, Meters)**:
   - Familia: `Segoe UI Variable Display`
   - Tamaño: `11 pt`, Peso: `SemiBold`, Espaciado entre letras: `+1.5 px` (Uppercase tracking)
3. **Telemetry & Unit Labels**:
   - Familia: `Cascadia Mono`
   - Tamaño: `9.5 pt`, Peso: `Regular`
   - Color: `TextSecondaryMuted`

---

## 9. Sistema de Espaciado y Cuadrícula

- **Grid Base**: Múltiplos de 4 píxeles (`4px`, `8px`, `12px`, `16px`, `24px`, `32px`).
- **Margen de Ventana**: `24px` en todos los bordes exteriores.
- **Separación entre Tarjetas**: `16px`.
- **Dimensiones de Ventana Predeterminadas**:
  - Anchura predeterminada: `780 px`
  - Altura predeterminada: `540 px`
  - Tamaño mínimo: `680 × 480 px` (para pantallas de portátiles)
  - Escalado responsivo: Compatible con escala de pantalla DPI al 100%, 125%, 150% y 200%.

---

## 10. Estados Visuales de la Aplicación

| Estado del Sistema | Indicador Header | Estado del Knob | Medidores | Mensaje Inferior |
|---|---|---|---|---|
| **Epicenter Activo (ON, B)** | `● ACTIVE` (Verde) | Arco iluminado en Ámbar | Activos con realce de salida | `48.0 kHz · 0.0 ms · DSP Active` |
| **Bypass (OFF o A)** | `● BYPASS` (Gris) | Arco en gris atenuado | Activos (Entrada == Salida) | `48.0 kHz · Linear Bypass` |
| **APO No Instalado / Fallo** | `⚠ NOT BOUND` (Naranja)| Atenuado con tooltip | Deshabilitados | `APO no enlazado en endpoint actual` |
| **IPC Desconectado** | `○ CONNECTING` (Azul) | Deshabilitado | En reposo | `Conectando con audiodg.exe...` |

---

## 11. Especificación del Control Rotary Knob

El control `RotaryKnob` es un control XAML personalizado con las siguientes capacidades táctiles de audio:

1. **Geometría y Visualización**:
   - Anillo exterior: 270 grados de arco (de 135° a 405°).
   - Rango: `0.0` a `100.0` (precisión de 0.1 internamente, formateado a entero en pantalla).
   - Ruta SVG `Path` calculada matemáticamente para renderizado acelerado por GPU (DirectX / Win2D).
2. **Interacciones de Puntero**:
   - **Vertical Drag**: Arrastre hacia arriba incrementa el valor; hacia abajo decrementa (250 píxeles para recorrer de 0 a 100).
   - **Horizontal Drag**: Opcional mediante tecla modificadora `Ctrl`.
   - **Rueda del Ratón**: $\pm 1.0$ por paso; con `Shift` presionado, $\pm 0.1$ para afinación fina.
   - **Doble Clic**: Restablece instantáneamente al valor predeterminado del preset activo (por defecto `75%`).
   - **Clic en Valor Numérico**: Abre un cuadro de entrada directa para teclear el número exacto.
3. **Control por Teclado**:
   - `Flecha Arriba` / `Flecha Derecha`: $+1.0$
   - `Flecha Abajo` / `Flecha Izquierda`: $-1.0$
   - `Page Up` / `Page Down`: $\pm 10.0$
   - `Home` / `End`: `0.0` / `100.0`

---

## 12. Especificación del Puente C# ↔ IPC (ApoIpcBridgeService)

La interfaz C# que conecta la UI con el archivo de memoria compartida:

```csharp
public interface IApoIpcBridgeService : IDisposable
{
    bool IsConnected { get; }
    void Initialize();
    void SetParameters(bool enabled, float intensity, uint abState, uint targetPreset);
    bool TryReadTelemetry(out ApoTelemetryData telemetry);
}
```

### Implementación Realtime-Safe desde C#:
- Se abre `MemoryMappedFile.OpenExisting("Global\\EpicenterDspIpcSharedMemory", MemoryMappedFileRights.ReadWrite)` (con fallback a `Local\`).
- Se adquiere una vista persistente `CreateViewAccessor(0, 4096, MemoryMappedFileAccess.ReadWrite)`.
- **Escritura**:
  ```csharp
  // Lectura del puntero no gestionado
  byte* ptr = null;
  accessor.SafeMemoryMappedViewHandle.AcquirePointer(ref ptr);
  EpicenterIpcSharedState* state = (EpicenterIpcSharedState*)ptr;

  // Actualización mediante Interlocked (hardware lock xadd)
  uint seq = (uint)Interlocked.Increment(ref *(int*)&state->control.sequence);
  Thread.MemoryBarrier();
  state->control.enabled = enabled ? 1u : 0u;
  state->control.intensity = Math.Clamp(intensity, 0.0f, 100.0f);
  state->control.abState = abState;
  state->control.targetPreset = targetPreset;
  Thread.MemoryBarrier();
  Interlocked.Increment(ref *(int*)&state->control.sequence);
  ```
- **Lectura**:
  La UI lee la estructura de telemetría a 60 FPS sin alterar los contadores de secuencia de audio.

---

## 13. Plan de Testing Automatizado del Paso 3

1. **IPC Bridge Tests (`EpicenterApp.Tests/IpcBridgeTests.cs`)**:
   - Conexión, desconexión y reconexión contra el simulador de memoria compartida.
   - Verificación de que `SetParameters` emite secuencias pares estables.
   - Verificación de sanitización ante inyección de `NaN` o valores $> 100$.
2. **ViewModel State Tests (`EpicenterApp.Tests/ViewModelTests.cs`)**:
   - Transiciones de estado: `Bypass -> Active`, `A -> B`.
   - Modificación de intensidad y sincronización con el servicio IPC.
   - Selección de presets y verificación de parámetros asignados.
3. **Meters & Telemetry Tests**:
   - Decodificación precisa de dBFS a alturas de barra en la UI.
   - Latch del indicador de Clip ante picos $> -0.1$ dBFS.
4. **Resilience & UI Thread Safety Tests**:
   - Simulación de latencia en IPC sin congelamiento del hilo principal de despacho XAML.

---

## 14. Arquitectura de Presets Iniciales (5 Presets Oficiales)

Los presets únicamente modifican los parámetros expuestos al usuario (`Intensity` y `A/B State`). El algoritmo interno permanece 100% protegido:

| Preset | Intensidad | Estado A/B | Descripción Acústica |
|---|---|---|---|
| **1. Default Studio** | `75 %` | `B (Active)` | Balance clásico de fábrica para música general y mezclas equilibradas. |
| **2. Gentle Warmth** | `45 %` | `B (Active)` | Refuerzo sutil y transparente para jazz, acústico, podcasts y masterización suave. |
| **3. Deep Extension** | `85 %` | `B (Active)` | Énfasis en la octava sub-grave más profunda ($20 - 45$ Hz) para música electrónica y hip-hop. |
| **4. Power Club** | `100 %` | `B (Active)` | Reconstrucción armónica completa de máxima autoridad para PA y sistemas de sonido de gran formato. |
| **5. Custom** | Variable | `B (Active)` | Posición definida libremente por el usuario. |

---

## 15. Atajos de Teclado Globales (UX de Estudio)

- **`Barra Espaciadora`**: Conmuta `MASTER ON / OFF` (Bypass rápido).
- **`A`**: Conmuta a modo `A (Original / Bypass)`.
- **`B`**: Conmuta a modo `B (Epicenter DSP Activo)`.
- **`Flecha Arriba / Abajo`**: Incrementa / decrementa la intensidad en $\pm 1\%$.
- **`Ctrl + 1..5`**: Carga directa de los Presets 1 al 5.
- **`Ctrl + ,`**: Abre el panel de Configuración (*Settings*).
