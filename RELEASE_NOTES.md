# Rclone Lite 1.6.0

## Novedades

- **Sincronización bidireccional (modo «Bidireccional»):** usa `rclone bisync`. Antes de aplicar se muestra una vista previa con copias, borrados propagados y archivos que cambiaron en ambos lados. Si hay conflicto, gana el más reciente y la otra versión se conserva junto a él como `.conflict1`.
- **Avisos de Windows cuando falla un trabajo en segundo plano**, con botón **Reintentar**. Los fallos pendientes también se ofrecen al abrir la app. Se puede desactivar en Ajustes.
- **Exclusiones guiadas:** plantillas (node_modules, .git, temporales de Office, Thumbs.db, vendor) y **«Ver qué queda fuera»**, que lista los archivos que las reglas dejan sin tocar.
- **Comprobar integridad:** botón «Comprobar» (`rclone check`) que muestra qué falta o difiere en el destino. En trabajos de tipo Mover avisa de que no conviene ejecutarlos con diferencias.
- **Límites de ancho de banda por horario**, p. ej. `09:00,2M 18:00,off`, con validación del formato.

## Límites

- La primera ejecución bidireccional de un par de carpetas debe hacerse a mano desde la app: los trabajos en segundo plano no la inician. Si cambias los filtros, hace falta repetirla.
- En bidireccional con varios destinos, cada uno se sincroniza con el origen por separado.
- La comprobación en destinos sin huella común (p. ej. cifrados) solo compara tamaño.
- El botón Reintentar de la notificación usa un protocolo `rclonelite://` registrado al mostrar el primer aviso. No se ha podido verificar el aspecto de la notificación en todas las versiones de Windows.
- El límite horario usa la hora del equipo y la aplica rclone a cada transferencia.

## Actualización

Descarga `RcloneLite-Setup-1.6.0.exe` e instálalo sobre la versión anterior. Tus datos permanecen en `%APPDATA%\RcloneLite`.

El instalador es x64 para Windows 10/11 y no está firmado, por lo que SmartScreen podría mostrar un aviso.

- Código original bajo licencia MIT; consulta `installer/LICENSES.txt` para licencias de terceros.
- Creado por Gonzalo Cruz Núñez.