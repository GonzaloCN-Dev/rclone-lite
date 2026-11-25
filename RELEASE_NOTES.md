# Rclone Lite 1.5.1

## Mejoras sobre 1.5.0

- **Actualizaciones y segundo plano:** la app detecta si un trabajo en segundo plano está transfiriendo y pospone la actualización. Además, antes de lanzar el instalador se marca la actualización para que los trabajos en segundo plano no arranquen mientras se reemplazan archivos.
- **Cola persistente:** si cierras la app con operaciones en cola o a medias, al abrirla se ofrece **reanudarlas** (rclone omite lo ya copiado). Si las descartas, no se vuelven a ofrecer.
- **Estado de la tarea de Windows independiente del idioma:** se consulta con PowerShell y valores numéricos, sin depender del texto de `schtasks`.

## Notas

- La detección de trabajos en segundo plano usa el bloqueo de transferencia compartido: solo ve un trabajo mientras transfiere.
- Las simulaciones no se guardan en la cola persistente.

## Actualización

Descarga `RcloneLite-Setup-1.5.1.exe` e instálalo sobre la versión anterior. Tus datos permanecen en `%APPDATA%\RcloneLite`.

El instalador es x64 para Windows 10/11 y no está firmado, por lo que SmartScreen podría mostrar un aviso.

- Código original bajo licencia MIT; consulta `installer/LICENSES.txt` para licencias de terceros.
- Creado por Gonzalo Cruz Núñez.