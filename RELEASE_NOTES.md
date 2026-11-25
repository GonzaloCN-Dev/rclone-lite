# Rclone Lite 1.5.0

## Errores y reintentos

- **Mensajes de error comprensibles:** los errores típicos de rclone (cuota agotada, permisos, sesión caducada, ruta inexistente, falta de espacio, red) se explican con su causa y qué hacer. El texto original sigue disponible en **Detalle técnico**, con botón para copiarlo.
- **Reintentar:** las operaciones y trabajos que fallan en Completadas tienen un botón **Reintentar** que repite el mismo origen, destino y opciones.

## Cola de transferencias

- Si inicias una transferencia mientras otra está en curso, queda **en cola** y empieza al terminar la anterior, en lugar de rechazarse. Puedes cancelarla desde la lista.
- Los trabajos en segundo plano esperan su turno si la app está transfiriendo, y al revés.

## Informe de simulación

- Las simulaciones y la vista previa antes de sincronizar muestran una **lista de archivos a copiar, actualizar y borrar**, con totales y tamaños. Los borrados aparecen primero y con aviso destacado.

## Segundo plano

- En Ajustes se muestra cuándo se ejecutó por última vez la tarea de Windows, la próxima comprobación y si los últimos trabajos tuvieron fallos y por qué.

## Notas

- La simulación no modifica nada, pero el contenido puede cambiar antes de aplicarla.
- Un informe muy grande muestra solo los primeros 3000 cambios; los totales incluyen todos.
- La protección de actualizaciones sigue cubriendo solo las transferencias de la app, no los trabajos en segundo plano.

## Actualización

Descarga `RcloneLite-Setup-1.5.0.exe` e instálalo sobre la versión anterior. Tus trabajos, remotos e historial permanecen en `%APPDATA%\RcloneLite`.

El instalador es x64 para Windows 10/11 y no está firmado, por lo que Windows SmartScreen podría mostrar un aviso de editor desconocido.

- Interfaz en español.
- Código original bajo licencia MIT; consulta `installer/LICENSES.txt` para las licencias de terceros.
- Creado por Gonzalo Cruz Núñez.