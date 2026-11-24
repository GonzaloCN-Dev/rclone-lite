# Rclone Lite 1.3.0

## Novedades

- **Trabajos programados con la app cerrada (opcional):** activa la opción en Ajustes para registrar una tarea del Programador de Windows. Se ejecuta con tu usuario y requiere que la sesión de Windows siga iniciada. Si Windows no puede registrar la tarea, los trabajos siguen funcionando mientras Rclone Lite esté abierto.
- **Pruebas con rclone real:** la suite ahora verifica transferencias, movimiento por lista, borrado selectivo y trabajos 1:N usando carpetas locales temporales.
- **Compilación y publicación automatizadas:** GitHub Actions compila y prueba en Windows; al subir una etiqueta `v*`, genera el instalador y su SHA-256 y publica ambos en la release.

## Actualización

Descarga `RcloneLite-Setup-1.3.0.exe` en los archivos adjuntos de esta release e instálalo sobre la versión anterior. Tus trabajos, remotos e historial permanecen en `%APPDATA%\RcloneLite`.

El instalador es x64 para Windows 10/11 y no está firmado, por lo que Windows SmartScreen podría mostrar un aviso de editor desconocido.

## Notas

- Interfaz en español.
- Código original bajo licencia MIT; consulta `installer/LICENSES.txt` para las licencias de terceros.
- Creado por Gonzalo Cruz Núñez.
