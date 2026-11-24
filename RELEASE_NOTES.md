# Rclone Lite 1.1.4

Simpler Google Drive setup.

## What's changed

- **Removed the own-credentials step.** Settings no longer has the "Google Drive: credenciales propias" section, and the Google Drive wizard no longer asks for a client ID and secret. Creating a Drive remote is just "sign in with your Google account", as in the other providers.
- **Remote editor kept.** "Editar" in Remotos still works for every remote; any option, including `client_id`, can still be changed there if you need it.
- **Throttling warning kept.** If Google answers `403 Quota exceeded` during a transfer, the Registro, a toast and the task show a notice.
- **Drive tuning kept.** With "auto tune" on, copies to or from Drive use a lighter API pacer and at least 16 transfers and 16 checkers.

## Install

Download `RcloneLite-Setup-1.1.4.exe` below and run it. Version 1.1.3 offers the update on its own. The installer is unsigned, so Windows SmartScreen may show an unknown-publisher warning.

## Notes

- 64-bit Windows 10/11.
- Spanish user interface.
- MIT license for original Rclone Lite code; see `installer/LICENSES.txt` for third-party components.
- Created by Gonzalo Cruz Núñez.