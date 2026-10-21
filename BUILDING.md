# Building and Flashing

**English** | [简体中文](BUILDING_zh-CN.md)

This document covers the source build, the factory updater, the partition layout, and the flashing workflow for the M5Stack Tab5.

## 1. Prerequisites

- **ESP-IDF v5.5.4** — the version pinned in `dependencies.lock`. Other 5.x versions may work but are not validated.
- **Python 3.11+**, Git, and the ESP-IDF toolchain installed by the ESP-IDF installer.
- **M5Stack Tab5** with a USB-C data cable.
- **Network access on the first build.** The ESP-IDF component manager downloads the managed components (`esp_hosted`, `esp_wifi_remote`, `eppp_link`, `esp_tinyusb`, `usb_host_msc`, `esp_codec_dev`, and others) into `managed_components/`. The downloaded versions are pinned by `dependencies.lock`.
- **Optional: ESP-ADF.** If `ADF_PATH` points to an ESP-ADF checkout, the top-level `CMakeLists.txt` also adds `audio_pipeline`, `esp-adf-libs`, and `audio_sal`. Without ESP-ADF the project still builds; the ADF-specific audio path is disabled and the non-ADF codec path/stub is used.

LVGL 9.2.2, `esp_lvgl_port`, the Tab5 BSP, `libssh_esp32`, and FatFs are vendored under `components/` and do not need to be downloaded.

## 2. Build The Main Firmware

Load the ESP-IDF environment first.

```powershell
# Windows PowerShell (adjust the path to your ESP-IDF installation)
. C:\esp\v5.5.4\esp-idf\export.ps1
```

```sh
# Linux / macOS
. "$IDF_PATH/export.sh"
```

Then build from the repository root:

```sh
idf.py build
```

The repository includes a known-good `sdkconfig` (target `esp32p4`, 16 MB QIO flash, 200 MHz PSRAM, ESP-Hosted SDIO, LVGL options). `sdkconfig.defaults` is the baseline used when regenerating the configuration. If you intentionally want to regenerate it:

```sh
rm sdkconfig
idf.py set-target esp32p4
idf.py build
```

> `sdkconfig` contains project-specific performance and memory settings. Prefer changing it through `idf.py menuconfig` and re-checking the diff instead of copying a configuration from another board.

### Optional ESP-ADF Audio Path

Set `ADF_PATH` before building if you have ESP-ADF installed:

```powershell
$env:ADF_PATH = "C:\Espressif\tools\esp-adf"
idf.py build
```

The top-level `CMakeLists.txt` also auto-detects `C:/Espressif/tools/esp-adf` on Windows when `ADF_PATH` is not set.

## 3. Partition Layout

The root `partitions.csv` and `Update_project/partitions.csv` must stay identical.

| Name | Type | SubType | Offset | Size |
| --- | --- | --- | --- | --- |
| `nvs` | data | nvs | `0x9000` | `0x6000` |
| `otadata` | data | ota | `0xF000` | `0x2000` |
| `phy_init` | data | phy | `0x11000` | `0x1000` |
| `main_app` | app | `ota_0` | `0x20000` | `0xF60000` |
| `factory` | app | factory | `0xF80000` | `0x80000` |

`main_app` is intentionally the first app partition at `0x20000`, which lets [bmorcelli/Launcher](https://github.com/bmorcelli/Launcher) select the SSH client when it extracts a full-flash image. The partition is named `main_app`, but its subtype must remain `ota_0` for ESP-IDF OTA boot selection.

The small factory updater lives at `0xF80000`. It checks the TF card for an update request before booting the main app.

This two-image layout is intentional: it gives the main application nearly all of the flash for future expansion while keeping OTA installation in a separate, minimal firmware that never overwrites itself. See [`Update_project/README.md`](Update_project/README.md) for the rationale and protocol.

## 4. Flash

### First Flash On A Blank Device

The main project build does **not** produce the factory updater, and `idf.py flash` does not populate the `factory` partition. For a blank device, flash the complete release bundle first, then use `idf.py app-flash` for day-to-day changes.

Full merged image (recommended for a first flash):

```powershell
python -m esptool --chip esp32p4 -p COMx -b 1500000 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m 0x0 release\flash_at_0x0\tab5_full_flash.bin
```

Alternatively, flash the individual partition images from `release/Partition_images/`; the exact commands and image map are in [`release/Partition_images/README.md`](release/Partition_images/README.md).

### Daily Development

After the bootloader, partition table, OTA data, and factory updater are already on the device, build and flash only the main application:

```sh
idf.py -p COMx app-flash monitor
```

`idf.py app-flash` writes the application to the `main_app` partition (`0x20000`). Do not flash the application at `0x0`.

`idf.py -p COMx flash monitor` also works for the main application, but it is not a substitute for the initial full-image flash on a blank device.

## 5. Build And Flash The Factory Updater

The updater is a separate, minimal ESP-IDF project with no LVGL:

```sh
cd Update_project
idf.py -B build_updater build
cd ..
```

Output: `Update_project/build_updater/tab5_factory_updater.bin`.

Manual flash at the factory partition offset:

```powershell
python -m esptool --chip esp32p4 -p COMx -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m 0xF80000 Update_project\build_updater\tab5_factory_updater.bin
```

On Windows, the helper script can flash the updater together with the bootloader, partition table, and OTA data:

```powershell
powershell -ExecutionPolicy Bypass -File .\Update_project\flash_factory_updater_layout.ps1 -Port COMx -UpdaterOnly
```

Run it without `-UpdaterOnly` to also flash `build/M5stack_tab5_ssh_client.bin` from the main build.

The updater reads `/sdcard/update/request.json` and `/sdcard/update/update.bin`, verifies that the firmware project name is `M5stack_tab5_ssh_client`, writes the image to `main_app`, clears the request, and switches the boot partition. See [`Update_project/README.md`](Update_project/README.md) for the design rationale, protocol, validation rules, and failure behavior.

## 6. OTA Endpoint

The main firmware fetches its OTA manifest from `OTA_UPDATE_SERVER_MANIFEST_URL` in `main/ota_update/ota_update_service.h`. The source tree ships a placeholder:

```c
#define OTA_UPDATE_SERVER_MANIFEST_URL "https://yourself.upload.server"
```

Set it to your own manifest URL before building. The server must return a manifest that lists the firmware version, size, SHA-256, download URL, changelog, and the optional factory-updater entry. The device verifies the manifest version, size, and SHA-256 before staging or installing anything.

## 7. Troubleshooting

- **`idf.py` is not recognized** — the ESP-IDF environment is not loaded. Run the export script for your shell.
- **Managed component download fails** — check network/proxy access to `components.espressif.com`. The exact versions are recorded in `dependencies.lock`.
- **Build fails after switching branches or ESP-IDF versions** — delete `build/` and `managed_components/`, then rebuild. Keep `sdkconfig` under version control.
- **The device boots but the screen stays off** — check that the full image (including the factory updater) was flashed, and verify the board revision and display configuration in `sdkconfig`.
- **In-app OTA is unavailable when booted by Launcher** — this is expected. Update through Launcher instead of the in-app OTA flow.
