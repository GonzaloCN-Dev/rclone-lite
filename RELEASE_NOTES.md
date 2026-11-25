# Rclone Lite 1.4.0

## Transferencias más seguras

- **Vista previa antes de sincronizar:** al ejecutar manualmente un trabajo de sincronización, Rclone Lite realiza primero una simulación. Revísala en Transferencias y confirma después para aplicar los cambios, incluidos los borrados del destino.
- **Evita conflictos:** mientras una transferencia de Rclone Lite esté activa, otra operación de transferencia —también un trabajo headless— se rechaza para evitar operaciones simultáneas sobre los mismos archivos.
- **Actualizaciones protegidas:** no se instala una actualización mientras haya transferencias activas en la aplicación.

## Trabajos programados

- La lista de trabajos muestra la próxima ejecución o indica cuándo una programación está vencida. El estado de la última ejecución sigue visible en la columna de estado.

## Nota

La vista previa es una simulación: el contenido de un remoto puede cambiar entre la simulación y la sincronización final.

## Actualización

Descarga `RcloneLite-Setup-1.4.0.exe` e instálalo sobre la versión anterior. Tus trabajos, remotos e historial permanecen en `%APPDATA%\RcloneLite`.

El instalador es x64 para Windows 10/11 y no está firmado, por lo que Windows SmartScreen podría mostrar un aviso de editor desconocido.

- Interfaz en español.
- Código original bajo licencia MIT; consulta `installer/LICENSES.txt` para las licencias de terceros.
- Creado por Gonzalo Cruz Núñez.
