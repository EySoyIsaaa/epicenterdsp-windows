# EpicenterDSP Windows

Aplicación Win32 nativa x64 que aplica `EpicenterDSPCore` en tiempo real a todo el audio compartido de Windows.

## Requisito único

Windows no permite que una aplicación normal reemplace el audio global sólo con *loopback*: eso dejaría sonando también la señal original. Por eso esta versión usa un cable de audio virtual como punto de inserción.

1. Descarga e instala **VB-CABLE** desde `https://vb-audio.com/Cable/`.
2. Reinicia Windows cuando termine la instalación del controlador.
3. Abre `EpicenterDSP Windows.exe` y pulsa **ACTIVAR**.

No hay que cambiar dispositivos manualmente. Al activar, la app:

- recuerda las salidas predeterminadas actuales;
- dirige Windows a `CABLE Input`;
- captura `CABLE Output`, aplica Epicenter y reproduce en tus bocinas/audífonos originales;
- restaura las salidas al desactivar o cerrar.

Un proceso guardián restaura el ruteo si la app termina inesperadamente. En el siguiente arranque también se recupera cualquier estado pendiente.

## Controles

- **ACTIVAR / DESACTIVAR:** conmuta el efecto para todo el audio de Windows.
- **Perilla:** arrastra verticalmente, usa la rueda o las flechas.
- **0 %:** bypass; la señal se reenvía sin pasar por el DSP.
- **1–100 %:** controla la intensidad interna, la mezcla seco/efecto y un refuerzo subgrave progresivo de hasta 5.5 dB.

La intensidad elegida se conserva en `HKCU\\Software\\EpicenterDSP Windows`.

## Compilar

Desde PowerShell:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\\build.ps1
```

El script configura CMake/Ninja, compila de forma estática, ejecuta las pruebas del DSP y deja el binario en `dist\\EpicenterDSP Windows.exe`.

## Limitaciones deliberadas

- Procesa audio compartido normal de Windows. Aplicaciones que abren un dispositivo en modo exclusivo no atraviesan el mezclador global.
- El puente interno trabaja en estéreo float a 48 kHz y deja que WASAPI convierta el formato de cada dispositivo.
- VB-CABLE es un controlador de terceros y no se redistribuye con esta aplicación.
