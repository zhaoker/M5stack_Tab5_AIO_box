# Factory Updater (`tab5_factory_updater`)

**English** | [简体中文](README_zh-CN.md)

A deliberately small maintenance firmware that lives in the `factory` partition of the M5Stack Tab5. It installs a new main firmware from the TF card and decides which application the device boots next.

This is a separate ESP-IDF project from the main application. It has no LVGL, no Wi-Fi, no network stack, and no Chinese UI resources.

## Why This Project Exists

The firmware is split into two independently built applications on purpose, not because of legacy layout constraints:

| Image | Partition | Size | Responsibility |
| --- | --- | --- | --- |
| Main application | `main_app` (`ota_0`) | 15.38 MB | SSH client, file manager, Wi-Fi, LVGL UI, OTA download client |
| Factory updater | `factory` | 512 KB | Validate and install `update.bin`, choose the next boot partition |

This layout was chosen to keep **future firmware expansion** and **OTA capability** working at the same time:

- **Maximum room for features.** The main application owns almost the entire 16 MB flash. New UI modules, fonts, protocol support, and media features can grow without fighting the updater for space.
- **The installer is independent of the installed application.** The code that writes `main_app` never runs from `main_app`. A corrupted or partially written main firmware can still be replaced through the same updater, because it runs from a different partition.
- **The updater stays replaceable but minimal.** The main application can download a new updater image, verify its size and SHA-256, and write it into the `factory` partition only after explicit user confirmation. Keeping this image small reduces the risk and time of that operation.
- **OTA remains a normal ESP-IDF flow.** `main_app` keeps subtype `ota_0`, even though its label is `main_app`, so `esp_ota_*` partition lookup, `otadata` boot selection, and the standard bootloader continue to work.
- **External launchers still work.** `main_app` is the first application partition at `0x20000`. Tools that extract the first app image from a full-flash dump, such as M5Launcher-style launchers, can still find the SSH client.
- **Recovery has a stable entry point.** The `factory` partition is a separate application subtype. When the main application is missing or a TF update is pending, the updater is the component that makes the boot decision.

The trade-off is that every firmware change that affects installation must be coordinated between the two projects. The rules below keep that manageable.

## Design Rules

- New user-facing features belong in the main application, not here.
- Keep this firmware under the 512 KB `factory` partition. It is built with `MINIMAL_BUILD` and must not pull in LVGL, Wi-Fi, or other large components.
- Updater output stays English-only and extremely simple: serial logs, a progress bar, and a solid-color error screen.
- Root `partitions.csv` and `Update_project/partitions.csv` must stay identical.
- Never change the updater installation protocol in only one project. See [Update Protocol](#update-protocol).

## Boot And Update Flow

```text
Main application (OTA page)
  check manifest
  download update.bin to /sdcard/update
  verify size and SHA-256
  write request.json
  user confirms install
  esp_ota_set_boot_partition(factory)
  restart
        |
        v
Factory updater (this project)
  mount TF card
  request.json present?
    no  -> verify main_app image, set boot to main_app, restart
    yes -> validate update.bin, write main_app, remove request.json,
           set boot to main_app, restart
        |
        v
Main application (new version)
```

The updater never writes the partition it is currently running from. If the TF card is unavailable or no update is requested, it boots the existing main application.

## TF Card Layout

```text
/sdcard/update/
├── request.json     # update request, written by the main application
├── update.bin       # main firmware image to install
├── progress.json    # live install progress, written by the updater
├── history.jsonl    # append-only install history
└── last_error.txt   # last failure reason, if any
```

The updater creates `/sdcard/update` when needed. `progress.json`, `history.jsonl`, and `last_error.txt` are diagnostic outputs; deleting them is safe.

### Update Protocol

`request.json` is written by the main application and looks like this:

```json
{
  "schema": 1,
  "action": "install_from_tf",
  "project": "M5stack_tab5_ssh_client",
  "model": "m5tab5",
  "chip": "esp32p4",
  "partition_scheme": "factory-updater-v1",
  "target_partition": "main_app",
  "firmware_path": "/sdcard/update/update.bin",
  "firmware_size": 1234567,
  "version": "1.2.3",
  "build_time": "2026-10-21 12:00:00",
  "sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
  "created_at": 1760000000
}
```

The current updater uses the **existence** of `request.json` as the trigger and always reads `/sdcard/update/update.bin`, writes `main_app`, and removes the request after a successful install. The JSON fields are descriptive and kept for forward compatibility. The updater re-validates the image itself rather than trusting the request.

To change the protocol, update both `write_request_json()` in `main/ota_update/ota_update_tasks.inc` and this updater.

### Image Validation

Before writing anything, the updater checks:

- The file exists, is non-empty, and fits inside the `main_app` partition.
- The ESP image magic is valid.
- The embedded application descriptor can be read.
- `project_name` is exactly `M5stack_tab5_ssh_client`, so an image built for another board or project is rejected.
- The target partition is not the currently running partition.
- `esp_ota_end()` accepts the complete image after writing.

After a successful write, it removes `request.json`, sets `main_app` as the boot partition, turns the backlight off, and restarts.

### Failure Behavior

On failure the updater:

- stops instead of booting a questionable image,
- shows a solid red error screen if the display has already been initialized,
- writes `/sdcard/update/last_error.txt`,
- appends an `install_failed` entry to `history.jsonl`,
- leaves `request.json` and `update.bin` in place.

A reset retries the same package. To stop retrying, remove `request.json` from the TF card, or replace `update.bin` with a valid image. If the main application itself is invalid and no TF update is possible, the updater refuses to boot it and remains in the maintenance path; reflash through USB or a Launcher.

## Status Output

The updater initializes the display only when an update is requested. During installation it draws a full-screen progress bar directly through the LCD panel driver, without LVGL. If installation fails after the display is initialized, the screen turns solid red and the device stops. Boots without an update request are silent and do not initialize the display.

The serial console is intentionally verbose. At `115200` baud it prints the partition dump, detected project and version, write progress, and the reason for any failure. This is the primary field-debugging channel.

## Build

Use the same ESP-IDF version as the main project (v5.5.4):

```sh
cd Update_project
idf.py -B build_updater build
```

Output:

```text
Update_project/build_updater/tab5_factory_updater.bin
```

Set `TAB5_UPDATER_VER` to override the version embedded in the image; `TAB5_PROJECT_VER` is used as a fallback.

The build is configured by `sdkconfig.defaults` and uses `MINIMAL_BUILD`. Do not add the main application's LVGL or Wi-Fi dependencies to this project.

## Flash

The factory updater is not produced by the main build, and `idf.py flash` in the repository root does not populate the `factory` partition. Flash it separately.

Manual flash at the partition offset:

```powershell
python -m esptool --chip esp32p4 -p COMx -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m 0xF80000 Update_project\build_updater\tab5_factory_updater.bin
```

On Windows, the helper script flashes the bootloader, partition table, OTA data, and updater together, using offsets read from `partitions.csv`:

```powershell
powershell -ExecutionPolicy Bypass -File .\Update_project\flash_factory_updater_layout.ps1 -Port COMx -UpdaterOnly
```

Without `-UpdaterOnly` it also flashes `build/M5stack_tab5_ssh_client.bin` at the `main_app` offset. Add `-EraseFlash` for a clean device.

For a blank device, flash the complete release image first; see [BUILDING.md](../BUILDING.md).

## Preparing A TF Update Package Manually

For testing without the OTA server, build the main application, then copy it to a TF card and create the request file:

```powershell
powershell -ExecutionPolicy Bypass -File .\Update_project\prepare_tf_update_package.ps1 -TfRoot E:\
```

This writes:

```text
E:\update\update.bin
E:\update\request.json
```

Insert the card, boot the device into the updater, and the install starts automatically.

## Partition Reference

| Name | Type | SubType | Offset | Size |
| --- | --- | --- | --- | --- |
| `nvs` | data | nvs | `0x9000` | `0x6000` |
| `otadata` | data | ota | `0xF000` | `0x2000` |
| `phy_init` | data | phy | `0x11000` | `0x1000` |
| `main_app` | app | `ota_0` | `0x20000` | `0xF60000` |
| `factory` | app | factory | `0xF80000` | `0x80000` |

`main_app` is a label, not a subtype. Its subtype must remain `ota_0` for ESP-IDF boot selection. Both copies of `partitions.csv` must match.

## How The Main Application Updates This Updater

The OTA page in the main application treats this image as a separate component:

1. Read the `updater` object from the OTA manifest.
2. Require version, URL, size, and SHA-256.
3. Reject images larger than the `factory` partition.
4. Download the image into RAM, verify size and SHA-256.
5. Erase and write the `factory` partition only after user confirmation.
6. Read back the new application descriptor and show the installed version.

The new updater takes effect the next time the device boots the factory partition, not immediately. The main application's UI labels this component "UPLOAD firmware"; it is the same `tab5_factory_updater` built here.

## Related Documentation

- [BUILDING.md](../BUILDING.md) — full build, partition, and flashing workflow.
- [DEVELOPMENT.md](../DEVELOPMENT.md) — main application architecture and maintenance notes (Chinese).
- [README.md](../README.md) — project overview and license status.
