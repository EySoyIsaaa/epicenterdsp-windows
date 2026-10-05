# EpicenterDSP Windows: estado de la prueba local

## Interfaz actual

Ejecuta `dist/EpicenterDSP Windows Actualizado/EpicenterDSP Windows.exe` normalmente,
sin administrador. Conserva toda la carpeta: esta compilacion WinUI no es un EXE
unico portable. El PC de desarrollo tiene sus runtimes instalados.

La perilla acepta arrastre vertical, rueda, Shift para ajuste fino, flechas,
Inicio/Fin y doble clic (75%). El usuario confirmo que responde. 0% solicita
bypass. OFF/ON y A/original/B/Epicenter son controles separados.
Logo e icono originales, negro/gris/rojo y label By EpicenterDSP Labs.

La interfaz no procesa audio ni instala el APO al pulsar REINTENTAR. Un estado
NOT BOUND es correcto mientras Windows no lo tenga vinculado. No implica que
debas seleccionar VB-CABLE; ese prototipo no es la ruta APO actual.
El medidor de entrada queda N/A porque el contrato IPC actual no proporciona
una medicion pre-DSP. La salida presenta PEAK y RMS agregados, no canales L/R
inventados. La duracion de un bloque no es la latencia completa del sistema.

## Instalacion: no repetir el metodo rechazado

`ACTIVAR_EPICENTER_APO.bat` conserva el experimento legacy con respaldo y
preflight. En este PC Windows deniega escribir en FxProperties incluso elevado.
No repetirlo para resolver la conexion: no toma posesion ni cambia esos permisos.
`DESACTIVAR_EPICENTER_APO.bat` conserva la restauracion de ese experimento.

El respaldo previo esta en `%PROGRAMDATA%/EpicenterDSP-TestInstall`. No borrarlo
ni editarlo. DisableProtectedAudioDG ya estaba en 1 antes de estos cambios:
el restorer conserva el valor previo real, no puede adivinar el anterior historico.
El apo_backup.json antiguo se conserva, pero respaldaba la propiedad UI, no EFX.

## Paquete nuevo preparado, todavia no instalado

Herramientas oficiales SDK/WDK verificadas se extrajeron dentro de `.tools`.
La identidad de desarrollo existe solo como archivos privados en `.dev-signing`:
no se agrego a almacenes de certificados del equipo. No compartir su PFX ni
archivo DPAPI ni incluir esas carpetas al distribuir la app.

El paquete componentizado esta en
`artifacts/apo-development/package-<id-generado>` (ruta mostrada por el empaquetador).
La DLL y dos catalogos estan firmados localmente; ambas INF pasaron InfVerif
y los catalogos se generaron con Inf2Cat sin errores/advertencias. Esta firma
no es de confianza para Windows y no es firma Microsoft/WHQL.
El candidato anterior esta marcado DO-NOT-INSTALL.

Se preparo especificamente para el ALC892 de este PC y su Salida digital/SPDIF,
no como instalador universal de todas las tarjetas. Mantiene el driver base
Microsoft; no incluye un nuevo driver kernel. El CLSID componentizado es distinto
del legacy, como requiere Microsoft para migrar del registro global al de dispositivo.
Core, interfaces COM y layout IPC de produccion se conservaron.

Falta autorizar una ruta de confianza/instalacion limitada, registrar los cambios
exactos y verificar relacion PnP, asociacion EFX, carga en audiodg, heartbeat y
audio compartido real. Validar/sellar archivos no demuestra que el audio funcione.
No se cambiaron Secure Boot, TestSigning ni ACLs de endpoints.

## Reproducir compilacion y pruebas

`BUILD_ALL.bat` compila x64 Release y ejecuta Core, ambas suites APO aisladas
y UI/servicios. No instala nada. Requiere Visual Studio 2022 C++, CMake y .NET 9
del entorno actual; .NET 9 no es LTS. La migracion al runtime LTS es trabajo
separado de distribucion antes de un lanzamiento comercial.

`scripts/Test-ApoInstaller.ps1` valida restauracion simulada sin escribir HKLM.
`scripts/Test-ApoBinary.ps1` inspecciona la factory real sin registrar la DLL.
`scripts/Build-ApoDevelopmentPackage.ps1 -Sign` reproduce el paquete offline.

Detalles y fuentes oficiales: `docs/APO_DEPLOYMENT_PLAN.md`.
