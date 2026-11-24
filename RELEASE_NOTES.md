# Rclone Lite 1.1.3

Edit saved connections, and a clear warning when Google throttles you.

## What's new

- **Edit saved remotes.** Remotos has a new "Editar" button. It opens the same dialog as the wizard with the current values. The name and type can't be changed. Password fields appear empty and only change if you type something; other fields change only if you edit them (empty a field to remove it). A "Parametros nuevos" box accepts extra `key=value` pairs. Nothing is saved when you change nothing.
- **Google Drive in the editor.** `client_id` and `client_secret` must change together, and the remote then reconnects in the browser. It is refused while transfers are running.
- **Throttling warning.** When Google answers `403 Quota exceeded` or `Rate limit exceeded` during a transfer, the app now shows a warning in the Registro, a toast, and a note on the task. If the remote uses rclone's shared key it suggests creating your own client ID in Ajustes.
- **Faster Google Drive transfers.** With "auto tune" on, copies to or from Drive use a lighter API pacer and at least 16 transfers and 16 checkers. Drive's speed varies a lot, so results will too.
- Secrets are still stored only by rclone in `rclone.conf`, never in the app's settings.

## Install

Download `RcloneLite-Setup-1.1.3.exe` below and run it. Version 1.1.2 offers the update on its own. The installer is unsigned, so Windows SmartScreen may show an unknown-publisher warning.

## Notes

- 64-bit Windows 10/11.
- Spanish user interface.
- MIT license for original Rclone Lite code; see `installer/LICENSES.txt` for third-party components.
- Created by Gonzalo Cruz Núñez.
