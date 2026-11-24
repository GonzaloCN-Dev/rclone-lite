# Rclone Lite 1.2.0

Una actualización centrada en proteger tus datos y hacer más cómodas las transferencias.

## Novedades

- **Importación segura de trabajos:** revisa un resumen antes de importar; las programaciones quedan desactivadas y se eliminan flags peligrosas. Se avisa de las sincronizaciones que pueden borrar archivos.
- **Almacenamiento más resistente:** copias de seguridad de los datos, recuperación desde `.bak` ante JSON dañado y avisos cuando no se pueden leer o guardar archivos.
- **Transferencias más fiables:** los trabajos 1:N continúan con otros destinos si uno falla; varios archivos sueltos se copian o mueven en lote.
- **Control de sobrescritura:** opción para no reemplazar archivos más nuevos del destino, en los ajustes generales y en cada trabajo.
- **Reautenticación OAuth:** Rclone Lite detecta errores habituales de token caducado y ofrece iniciar sesión de nuevo.
- **Borrados más claros:** se acumulan los errores de cada elemento y las operaciones se añaden al historial.
- **Atajos en el explorador:** F2 renombrar, F5 copiar, F6 mover, F7 crear carpeta y F8 eliminar. No se activan mientras se escribe en la ruta.

## Actualización

Descarga `RcloneLite-Setup-1.2.0.exe` en los archivos adjuntos de esta release e instálalo sobre la versión anterior. Tus trabajos, remotos e historial permanecen en `%APPDATA%\RcloneLite`.

El instalador es x64 para Windows 10/11 y no está firmado, por lo que Windows SmartScreen podría mostrar un aviso de editor desconocido.

## Notas

- Interfaz en español.
- Código original bajo licencia MIT; consulta `installer/LICENSES.txt` para las licencias de terceros.
- Creado por Gonzalo Cruz Núñez.
