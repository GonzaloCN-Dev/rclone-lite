# Rclone Lite 1.1.1

Much faster uploads of many small files to Google Drive.

## What's new

- **Automatic speed tuning for Google Drive.** Transfers to or from a Drive remote now run with 16 parallel transfers/checks and a faster request pacer (`--drive-pacer-min-sleep=2ms --drive-pacer-burst=500`). In tests with small files (project folders, `htdocs`) throughput was about 3.5x higher (average 0.7 -> 2.4 files per second over two interleaved runs; the absolute rate varies with Drive load). Large files are not affected. It can be turned off in Settings, and your own `--drive-pacer-*` flags always win.
- **Quick filter.** The job dialog has a button that excludes `node_modules`, `vendor` and `.git` with one click.

## Tips for huge folders

Drive limits file creation rate, so the number of files matters more than their size. Exclude regenerable folders, zip what you want to keep, and use your own Google client ID for a private quota.

## Install

Download `RcloneLite-Setup-1.1.1.exe` below and run it. Version 1.1.0 offers the update on its own. The installer is unsigned, so Windows SmartScreen may show an unknown-publisher warning.

## Notes

- 64-bit Windows 10/11.
- Spanish user interface.
- MIT license for original Rclone Lite code; see `installer/LICENSES.txt` for third-party components.
- Created by Gonzalo Cruz Núñez.