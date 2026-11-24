# Rclone Lite 1.1.0

Live transfer speed, a fix for unexpected crashes, automatic updates and a smarter installer.

## What's new

- **Live speed chart.** The transfers panel shows the current speed in KB/s or MB/s with a real-time chart of the last minute, for copy, move and sync.
- **Crash fix.** The app could close without warning during a transfer when rclone reported a `null` value in its JSON log. All external JSON is now read defensively, worker threads can no longer take the process down, and any fatal error is written to `%APPDATA%\RcloneLite\errors.log`.
- **Automatic updates.** Rclone Lite checks GitHub for new releases at startup (it can be turned off in Settings), asks before updating, verifies the SHA-256 of the download and runs the installer. The app closes itself during the update and reopens when it finishes. A **Check for updates** button is available in Settings.
- **Smarter installer.** It detects rclone and WinFsp and, if either is missing, offers to download and install the latest version (checksum verified). The installer is now 9.6 MB instead of 28.6 MB because rclone is no longer bundled.

## Install

Download `RcloneLite-Setup-1.1.0.exe` below and run it. If you already have 1.0.0 installed, it updates in place. The installer is unsigned, so Windows SmartScreen may show an unknown-publisher warning.

## Notes

- 64-bit Windows 10/11.
- Spanish user interface.
- MIT license for original Rclone Lite code; see `installer/LICENSES.txt` for third-party components.
- Created by Gonzalo Cruz Núñez.