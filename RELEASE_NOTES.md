# Rclone Lite 1.3.2

## Cambios

- Las actualizaciones no se inician mientras haya transferencias activas en la aplicación. Si comienza una transferencia durante la descarga, la instalación se aplaza y puedes volver a intentarlo cuando termine.

Esta protección no detecta trabajos programados que se estén ejecutando en un proceso independiente con la aplicación cerrada. Espera a que esos trabajos terminen antes de actualizar.

## Actualización

Descarga `RcloneLite-Setup-1.3.2.exe` e instálalo sobre la versión anterior. Tus trabajos, remotos e historial permanecen en `%APPDATA%\RcloneLite`.

El instalador es x64 para Windows 10/11 y no está firmado, por lo que Windows SmartScreen podría mostrar un aviso de editor desconocido.

- Interfaz en español.
- Código original bajo licencia MIT; consulta `installer/LICENSES.txt` para las licencias de terceros.
- Creado por Gonzalo Cruz Núñez.
