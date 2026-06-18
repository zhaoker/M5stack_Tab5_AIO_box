# Partition Images Flashing Guide

This folder contains individual firmware images for each flash region used by the ESP32-P4 M5Stack Tab5 SSH client firmware.

## English

Run the commands below from this `Partition_images` folder. Replace `COMx` with your serial port, for example `COM3` on Windows.

### Flash All Partition Images

```powershell
python -m esptool --chip esp32p4 --baud 1500000 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m 0x2000 bootloader.bin 0x8000 partition_table.bin 0xF000 ota_data_initial.bin 0x20000 tab5_factory_updater.bin 0xA0000 M5stack_tab5_ssh_client.bin
```

With an explicit serial port:

```powershell
python -m esptool --chip esp32p4 -p COMx --baud 1500000 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m 0x2000 bootloader.bin 0x8000 partition_table.bin 0xF000 ota_data_initial.bin 0x20000 tab5_factory_updater.bin 0xA0000 M5stack_tab5_ssh_client.bin
```

### Flash Only The Main Application

Use this when the bootloader, partition table, OTA data, and factory updater are already correct on the device, and you only want to update the main firmware:

```powershell
python -m esptool --chip esp32p4 -p COMx --baud 1500000 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m 0xA0000 M5stack_tab5_ssh_client.bin
```

Do not flash `M5stack_tab5_ssh_client.bin` to `0x0`. It belongs at the `main_app` partition offset `0xA0000`.

### Image Map

| Offset | File | Purpose |
| --- | --- | --- |
| `0x2000` | `bootloader.bin` | ESP-IDF bootloader |
| `0x8000` | `partition_table.bin` | Flash partition table |
| `0xF000` | `ota_data_initial.bin` | OTA boot selection data |
| `0x20000` | `tab5_factory_updater.bin` | Factory updater / UPLOAD firmware |
| `0xA0000` | `M5stack_tab5_ssh_client.bin` | Main application firmware |

## 中文

下面的命令请在当前 `Partition_images` 目录中执行。请把 `COMx` 替换为实际串口号，例如 Windows 上的 `COM3`。

### 刷入全部分区镜像

```powershell
python -m esptool --chip esp32p4 --baud 1500000 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m 0x2000 bootloader.bin 0x8000 partition_table.bin 0xF000 ota_data_initial.bin 0x20000 tab5_factory_updater.bin 0xA0000 M5stack_tab5_ssh_client.bin
```

指定串口的命令：

```powershell
python -m esptool --chip esp32p4 -p COMx --baud 1500000 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m 0x2000 bootloader.bin 0x8000 partition_table.bin 0xF000 ota_data_initial.bin 0x20000 tab5_factory_updater.bin 0xA0000 M5stack_tab5_ssh_client.bin
```

### 只刷主程序

当设备上的 bootloader、分区表、OTA 数据、factory updater 都已经正确，只想更新主程序时，使用下面的命令：

```powershell
python -m esptool --chip esp32p4 -p COMx --baud 1500000 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m 0xA0000 M5stack_tab5_ssh_client.bin
```

不要把 `M5stack_tab5_ssh_client.bin` 刷到 `0x0`。它属于 `main_app` 分区，偏移地址是 `0xA0000`。

### 镜像对应关系

| 偏移地址 | 文件 | 说明 |
| --- | --- | --- |
| `0x2000` | `bootloader.bin` | ESP-IDF 启动加载器 |
| `0x8000` | `partition_table.bin` | Flash 分区表 |
| `0xF000` | `ota_data_initial.bin` | OTA 启动选择数据 |
| `0x20000` | `tab5_factory_updater.bin` | Factory updater / UPLOAD 固件 |
| `0xA0000` | `M5stack_tab5_ssh_client.bin` | 主程序固件 |

## Partition Table / 分区表

| Name | Type | SubType | Offset | Size | Description |
| --- | --- | --- | --- | --- | --- |
| `nvs` | `data` | `nvs` | `0x9000` | `0x6000` | Non-volatile settings storage / 非易失设置存储 |
| `otadata` | `data` | `ota` | `0xF000` | `0x2000` | OTA boot metadata / OTA 启动元数据 |
| `phy_init` | `data` | `phy` | `0x11000` | `0x1000` | PHY calibration/init data / PHY 初始化数据 |
| `factory` | `app` | `factory` | `0x20000` | `0x80000` | Factory updater application / Factory updater 应用 |
| `main_app` | `app` | `ota_0` | `0xA0000` | `0xF60000` | Main SSH client application / SSH 客户端主程序 |

Notes:

- `nvs` and `phy_init` are data partitions. They are not included as standalone `.bin` files in this folder.
- `otadata` is included so a full partition-image flash can set the boot selection state correctly.
- The main application partition is named `main_app`, but its subtype is `ota_0`; keep this layout unchanged for ESP-IDF OTA boot support.

说明：

- `nvs` 和 `phy_init` 是数据分区，本目录没有提供独立 `.bin` 文件。
- `otadata` 已包含在本目录中，完整刷入分区镜像时会写入正确的启动选择状态。
- 主程序分区名称是 `main_app`，但 subtype 是 `ota_0`；请保持这个布局不变，以便 ESP-IDF OTA 启动机制正常工作。
