# M5Stack Tab5 AIO Box

**面向 M5Stack Tab5 的 ESP32-P4 SSH 终端、文件管理器、媒体播放器与 OTA 维护固件。**

[English](README.md) | **简体中文**

[产品介绍与快速开始](release/README_zh-CN.md) | [English release](release/README.md) | [繁體中文](release/README_zh-TW.md)

本仓库提供完整固件源码。此前发布的发布包——产品页、截图、快速开始指南和预编译固件——已原样保留在 [`release/`](release/) 中。

## 功能

- **SSH 终端**——支持保存多台服务器、密码认证、PTY Shell、ANSI/VT100 解析，以及基于 LVGL 画布的 UTF-8 与宽字符渲染。
- **Wi-Fi 管理**——扫描、连接、保存凭据、自动连接、IPv4/IPv6 状态。ESP32-P4 通过 SDIO 使用板载 ESP32-C6（`esp_hosted` + `esp_wifi_remote`）。
- **TF 卡文件管理器**——浏览、复制、剪切、粘贴、重命名、删除、批量操作，带 EXIF 信息的文本与图片查看器，MP3/FLAC 播放。
- **Web 文件管理器**——通过 Wi-Fi 在浏览器中访问文件。
- **OTA 维护**——检查主固件和 factory updater 更新，下载到 TF 卡，校验版本与安装包。
- **系统工具**——电池/充电状态、三指截图、加密或明文配置备份、可配置的待机屏保、运行时语言选择（English、简体中文、繁體中文）。

## 架构：刻意采用双固件镜像

本项目有意提供**两个分别构建的 ESP-IDF 应用**，而不是单一整体镜像：

```text
factory（512 KB）                 main_app（15.38 MB，子类型 ota_0）
┌────────────────────┐           ┌────────────────────────────────┐
│ tab5_factory_      │           │ 完整应用：LVGL、SSH、          │
│ updater            │  -------> │ 文件管理器、Wi-Fi、OTA 客户端  │
│ TF 卡安装、启动选择│           │                                │
└────────────────────┘           └────────────────────────────────┘
```

- `main_app` 承载完整的用户固件，占用几乎全部 16 MB Flash，为未来功能、字体、协议和更大的 UI 资源预留空间。
- `factory` 承载最小化 updater，不含 LVGL 和 Wi-Fi。它校验 TF 卡镜像、写入 `main_app`，并选择下一个启动分区。
- 安装器独立于被安装的应用：即使 `main_app` 镜像损坏，只要 factory updater 完好，无需调试器也能替换。
- `main_app` 使用标签 `main_app`，但子类型仍为 `ota_0`，因此标准 ESP-IDF OTA API 与 `otadata` 启动选择继续有效。它同时是位于 `0x20000` 的第一个应用分区，兼容 Launcher 类工具。
- 主程序可以更新 factory updater 自身：下载小镜像、校验大小与 SHA-256，并在用户确认后写入 `factory` 分区。

这一拆分是刻意设计，目的是在保持 OTA 可靠的同时让固件持续扩展。updater 的协议、安全检查、构建与刷写步骤见 [`Update_project/README_zh-CN.md`](Update_project/README_zh-CN.md)。

## 硬件

| 项目 | 参数 |
| --- | --- |
| 开发板 | M5Stack Tab5 |
| SoC | ESP32-P4 |
| 无线 | ESP32-C6，通过 SDIO 连接 |
| Flash / PSRAM | 16 MB / 32 MB |
| 显示屏 | 720 x 1280 MIPI DSI 触摸屏 |
| 存储 | TF 卡、USB-C |
| 输入 | 触摸屏、可选实体键盘 |

## 仓库结构

```text
main/                  主固件：UI、SSH、终端、文件管理器、音频、OTA
components/            本地集成的第三方组件：LVGL 9.2.2、esp_lvgl_port、
                       Tab5 BSP、libssh_esp32、FatFs
Update_project/        factory 分区小固件；设计说明与升级协议见
                       Update_project/README_zh-CN.md
doc/                   M5Stack Tab5 硬件参考 PDF
release/               原发布包：产品 README、图片、快速开始指南、预编译固件
BUILDING.md            工具链、构建、分区与刷写说明
BUILDING_zh-CN.md      同上，简体中文
DEVELOPMENT.md         详细架构与开发手册（简体中文）
THIRD_PARTY_LICENSES.md
```

`managed_components/` 由 ESP-IDF 组件管理器在首次构建时生成，刻意不提交到仓库。

## 构建

项目目标为 **ESP-IDF v5.5.4** 和 `esp32p4`。

```sh
. "$IDF_PATH/export.sh"     # Windows: %IDF_PATH%\export.bat 或 export.ps1
idf.py build
```

完整的工具链配置、可选 ESP-ADF 音频支持、分区布局、updater 构建和刷写流程见 [BUILDING_zh-CN.md](BUILDING_zh-CN.md)。

## 预编译固件

最近发布的镜像位于发布包中：

- `release/flash_at_0x0/tab5_full_flash.bin`——合并镜像，从地址 `0x0` 刷入。
- `release/Partition_images/`——各分区独立镜像及 esptool 命令。
- `release/Partition_images/README.md`——刷写细节。

预编译镜像只是发布快照，可能和当前源码不一致。需要最新改动时请从源码重新构建。

## 文档

- [BUILDING_zh-CN.md](BUILDING_zh-CN.md)——构建、刷写、updater 与分区参考。
- [Update_project/README_zh-CN.md](Update_project/README_zh-CN.md)——factory updater 设计、TF 卡升级协议与独立构建。
- [DEVELOPMENT.md](DEVELOPMENT.md)——架构、模块边界、关键流程与维护说明（简体中文）。
- [BUILDING.md](BUILDING.md)——English build and flashing guide.
- [Update_project/README.md](Update_project/README.md)——English factory updater documentation.
- [release/QUICK_START_GUIDE_zh-CN.md](release/QUICK_START_GUIDE_zh-CN.md)——简体中文快速使用手册。
- [release/QUICK_START_GUIDE.md](release/QUICK_START_GUIDE.md)——English quick-start guide.
- [release/QUICK_START_GUIDE_zh-TW.md](release/QUICK_START_GUIDE_zh-TW.md)——繁體中文快速使用手冊。

## 隐私

SSH 凭据使用 AES-256-GCM 加密存储。配置备份可导出为加密或明文压缩包。除 OTA 清单和固件下载请求外，固件不会将 SSH、Wi-Fi 或文件数据发送到外部服务。

## 许可证

项目自身源码与文档采用 [Apache License 2.0](LICENSE) 许可。署名与版权声明见 [NOTICE](NOTICE)；随附组件的许可证见 [THIRD_PARTY_LICENSES_zh-CN.md](THIRD_PARTY_LICENSES_zh-CN.md)。

随附的第三方组件仍遵循各自许可证。其中 libssh 采用 LGPL-2.1；本仓库已包含其完整对应源码，可以使用修改后的 libssh 重新构建或重新链接固件。
