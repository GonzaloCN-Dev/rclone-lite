# Rclone Lite 1.1.6

Your own Google credentials again, because rclone's shared key no longer signs in.

## What's changed

- **Google now blocks rclone's shared client ID.** Signing in with it fails with "Access blocked: Rclone no ha completado el proceso de verificacion de Google ... Error 403: access_denied". It is rclone's key, not your account, and rclone has announced it is being retired during 2026.
- **Your own Client ID and Client secret are back.** Settings has the "Google Drive: credenciales propias" section again, and the Google Drive wizard asks for them. Editing a Drive remote also keeps the optional fields and the "Reautenticar en el navegador" button.
- **Clearer text.** The Drive wizard and the editor now say that the shared key can fail, instead of suggesting to keep using it.
- Kept from 1.1.5: "Reautenticar en el navegador" for Drive, OneDrive, Dropbox, Box and pCloud. Kept from 1.1.3: the editor and the throttling warning.

## How to create your credentials

1. In the Google Cloud console, enable the Google Drive API.
2. Create an OAuth client of type "Desktop app" and set the consent screen to "In production" (in "Testing" the access expires after 7 days).
3. Paste the Client ID and Client secret in Rclone Lite and sign in in the browser. The guide button in Settings opens the rclone instructions.

## Install

Download `RcloneLite-Setup-1.1.6.exe` below and run it. Version 1.1.5 offers the update on its own. The installer is unsigned, so Windows SmartScreen may show an unknown-publisher warning.

## Notes

- 64-bit Windows 10/11.
- Spanish user interface.
- MIT license for original Rclone Lite code; see `installer/LICENSES.txt` for third-party components.
- Created by Gonzalo Cruz Núñez.