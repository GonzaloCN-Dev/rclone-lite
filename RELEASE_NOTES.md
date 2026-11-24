# Rclone Lite 1.1.2

Your own Google client ID, set from the app.

## What's new

- **Own Google Drive credentials.** Settings has a new "Google Drive: credenciales propias" section. Pick a Drive remote, enter your `client_id` and `client_secret`, and press "Guardar y reconectar" to authorize again in the browser. It also shows whether the remote uses rclone's shared key.
- **In the new-remote wizard.** Creating a Google Drive remote now has optional Client ID and Client secret fields.
- **Why it matters.** rclone's shared key has a quota common to all its users. When it runs out Google answers `403 Quota exceeded ... Requests per minute` and transfers drop to a few KB/s. With your own client ID the quota is yours. Creating one is free; the "Cómo crear mis credenciales" button opens the guide.
- The client secret is stored only by rclone in `rclone.conf`, never in the app's settings. Changing credentials is refused while transfers are running, since reconnecting would interrupt them.

## Install

Download `RcloneLite-Setup-1.1.2.exe` below and run it. Version 1.1.1 offers the update on its own. The installer is unsigned, so Windows SmartScreen may show an unknown-publisher warning.

## Notes

- 64-bit Windows 10/11.
- Spanish user interface.
- MIT license for original Rclone Lite code; see `installer/LICENSES.txt` for third-party components.
- Created by Gonzalo Cruz Núñez.