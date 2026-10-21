# 构建与刷写

[English](BUILDING.md) | **简体中文**

本文档介绍 M5Stack Tab5 的源码构建、factory updater、分区布局与刷写流程。

## 1. 前置条件

- **ESP-IDF v5.5.4**——与 `dependencies.lock` 锁定的版本一致。其他 5.x 版本可能可用，但未经验证。
- **Python 3.11+**、Git，以及由 ESP-IDF 安装器安装的 ESP-IDF 工具链。
- **M5Stack Tab5** 和一根 USB-C 数据线。
- **首次构建需要网络。** ESP-IDF 组件管理器会将托管组件（`esp_hosted`、`esp_wifi_remote`、`eppp_link`、`esp_tinyusb`、`usb_host_msc`、`esp_codec_dev` 等）下载到 `managed_components/`。下载版本由 `dependencies.lock` 固定。
- **可选：ESP-ADF。** 如果 `ADF_PATH` 指向 ESP-ADF 检出目录，顶层 `CMakeLists.txt` 还会加入 `audio_pipeline`、`esp-adf-libs` 和 `audio_sal`。没有 ESP-ADF 时项目仍可构建；ADF 专用音频路径会被禁用，改用非 ADF 编解码路径或桩实现。

LVGL 9.2.2、`esp_lvgl_port`、Tab5 BSP、`libssh_esp32` 和 FatFs 已集成在 `components/` 下，无需下载。

## 2. 构建主固件

先加载 ESP-IDF 环境。

```powershell
# Windows PowerShell（按实际 ESP-IDF 安装路径调整）
. C:\esp\v5.5.4\esp-idf\export.ps1
```

```sh
# Linux / macOS
. "$IDF_PATH/export.sh"
```

然后在仓库根目录构建：

```sh
idf.py build
```

仓库包含一份已知可用的 `sdkconfig`（目标 `esp32p4`、16 MB QIO Flash、200 MHz PSRAM、ESP-Hosted SDIO、LVGL 选项）。`sdkconfig.defaults` 是重新生成配置时使用的基线。如确有需要重新生成：

```sh
rm sdkconfig
idf.py set-target esp32p4
idf.py build
```

> `sdkconfig` 包含项目专用的性能和内存设置。优先通过 `idf.py menuconfig` 修改并复查差异，不要直接照搬其他开发板的配置。

### 可选 ESP-ADF 音频路径

如果已安装 ESP-ADF，可在构建前设置 `ADF_PATH`：

```powershell
$env:ADF_PATH = "C:\Espressif\tools\esp-adf"
idf.py build
```

在 Windows 上未设置 `ADF_PATH` 时，顶层 `CMakeLists.txt` 也会自动检测 `C:/Espressif/tools/esp-adf`。

## 3. 分区布局

根目录 `partitions.csv` 与 `Update_project/partitions.csv` 必须保持一致。

| 名称 | 类型 | 子类型 | 偏移 | 大小 |
| --- | --- | --- | --- | --- |
| `nvs` | data | nvs | `0x9000` | `0x6000` |
| `otadata` | data | ota | `0xF000` | `0x2000` |
| `phy_init` | data | phy | `0x11000` | `0x1000` |
| `main_app` | app | `ota_0` | `0x20000` | `0xF60000` |
| `factory` | app | factory | `0xF80000` | `0x80000` |

`main_app` 有意作为 `0x20000` 处的第一个应用分区，这样 [bmorcelli/Launcher](https://github.com/bmorcelli/Launcher) 在提取整包镜像时能选中 SSH 客户端。分区标签是 `main_app`，但子类型必须保持 `ota_0`，供 ESP-IDF OTA 启动选择使用。

小型 factory updater 位于 `0xF80000`。它在启动主程序前检查 TF 卡中的升级请求。

这种双镜像布局是刻意设计：主应用获得几乎全部 Flash 供未来扩展，同时把 OTA 安装放在独立的最小固件中，后者不会覆盖自身。设计理由与协议见 [`Update_project/README_zh-CN.md`](Update_project/README_zh-CN.md)。

## 4. 刷写

### 空白设备首次刷写

主项目构建**不会**生成 factory updater，`idf.py flash` 也不会写入 `factory` 分区。空白设备请先刷完整发布包，之后日常修改再用 `idf.py app-flash`。

完整合并镜像（推荐首次刷写）：

```powershell
python -m esptool --chip esp32p4 -p COMx -b 1500000 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m 0x0 release\flash_at_0x0\tab5_full_flash.bin
```

也可以刷写 `release/Partition_images/` 中的各分区镜像；具体命令和镜像映射见 [`release/Partition_images/README.md`](release/Partition_images/README.md)。

### 日常开发

当 bootloader、分区表、OTA 数据和 factory updater 已在设备上时，只需构建并刷写主应用：

```sh
idf.py -p COMx app-flash monitor
```

`idf.py app-flash` 会把应用写入 `main_app` 分区（`0x20000`）。不要把应用刷到 `0x0`。

`idf.py -p COMx flash monitor` 也可用于刷写主应用，但不能替代空白设备的首次整包刷写。

## 5. 构建并刷写 factory updater

updater 是一个独立的最小 ESP-IDF 项目，不含 LVGL：

```sh
cd Update_project
idf.py -B build_updater build
cd ..
```

输出：`Update_project/build_updater/tab5_factory_updater.bin`。

手动刷写到 factory 分区偏移：

```powershell
python -m esptool --chip esp32p4 -p COMx -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m 0xF80000 Update_project\build_updater\tab5_factory_updater.bin
```

在 Windows 上，可使用辅助脚本将 bootloader、分区表、OTA 数据和 updater 一起刷写：

```powershell
powershell -ExecutionPolicy Bypass -File .\Update_project\flash_factory_updater_layout.ps1 -Port COMx -UpdaterOnly
```

不加 `-UpdaterOnly` 时，还会把主构建生成的 `build/M5stack_tab5_ssh_client.bin` 刷入。

updater 读取 `/sdcard/update/request.json` 和 `/sdcard/update/update.bin`，校验固件项目名是否为 `M5stack_tab5_ssh_client`，将镜像写入 `main_app`，清除请求并切换启动分区。设计理由、协议、校验规则和失败行为见 [`Update_project/README_zh-CN.md`](Update_project/README_zh-CN.md)。

## 6. OTA 服务地址

主固件从 `main/ota_update/ota_update_service.h` 中的 `OTA_UPDATE_SERVER_MANIFEST_URL` 获取 OTA 清单。源码中默认为占位地址：

```c
#define OTA_UPDATE_SERVER_MANIFEST_URL "https://yourself.upload.server"
```

构建前请改为你自己的清单地址。服务器返回的清单需包含固件版本、大小、SHA-256、下载地址、更新日志，以及可选的 factory updater 条目。设备在暂存或安装前会校验清单版本、大小和 SHA-256。

## 7. 故障排查

- **`idf.py` 无法识别**——未加载 ESP-IDF 环境。为当前 Shell 运行对应的 export 脚本。
- **托管组件下载失败**——检查到 `components.espressif.com` 的网络或代理。确切版本记录在 `dependencies.lock`。
- **切换分支或 ESP-IDF 版本后构建失败**——删除 `build/` 和 `managed_components/` 后重新构建。将 `sdkconfig` 纳入版本管理。
- **设备启动但屏幕不亮**——确认已刷入完整镜像（包含 factory updater），并检查 `sdkconfig` 中的开发板版本与显示配置。
- **通过 Launcher 启动时应用内 OTA 不可用**——这是预期行为。请通过 Launcher 更新，而不是应用内 OTA 流程。
