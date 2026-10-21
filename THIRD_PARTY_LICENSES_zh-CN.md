# 第三方许可证与声明

[English](THIRD_PARTY_LICENSES.md) | **简体中文**

本仓库包含项目自身源码，以及本地集成的第三方组件、生成的字体数据和预编译发布包。项目自身源码采用 [Apache License 2.0](LICENSE) 许可，署名见 [NOTICE](NOTICE)。以下组件仍遵循各自的许可证。

## 本地集成的组件

| 组件 | 版本 | 许可证 | 许可证文件 |
| --- | --- | --- | --- |
| LVGL | 9.2.2 | MIT | `components/lvgl/LICENCE.txt` |
| esp_lvgl_port | 2.5.0 | Apache-2.0 | `components/espressif__esp_lvgl_port/license.txt` |
| M5Stack Tab5 BSP | 本地 | Apache-2.0 | `components/m5stack_tab5/LICENSE` |
| libssh_esp32 / libssh | 本地移植 | LGPL-2.1；部分文件为 BSD-2-Clause | `components/libssh_esp32/COPYING`、`components/libssh_esp32/BSD` |
| FatFs | R0.15 patch2 | FatFs 许可证（BSD 风格） | `components/fatfs/src/ff.c` 文件头 |
| esp_lcd_st7121 | 本地 | Apache-2.0 | `components/esp_lcd_st7121/esp_lcd_st7121.c` 中的 SPDX 声明 |

`libssh_esp32` 以 LGPL-2.1 分发。本仓库包含完整的对应源码，因此可以用修改过或重新链接的 libssh 重新构建固件。部分 libssh 文件采用 BSD-2-Clause 许可证；见 `components/libssh_esp32/BSD`。

## 构建期间获取的托管组件

ESP-IDF 组件管理器会在首次构建时将以下组件下载到 `managed_components/`。它们不会提交到本仓库。每个组件在其下载目录中带有各自的许可证文件。

`espressif/esp_hosted`、`espressif/esp_wifi_remote`、`espressif/eppp_link`、`espressif/esp_serial_slave_link`、`espressif/esp_tinyusb`、`espressif/usb_host_msc`、`espressif/usb_host_hid`、`espressif/usb_host_uvc`、`espressif/esp_codec_dev`、`espressif/esp_lcd_ili9881c`、`espressif/esp_lcd_st7703`、`espressif/esp_lcd_touch`、`espressif/esp_lcd_touch_gt911`、`espressif/esp_lcd_touch_st7123`、`espressif/esp_cam_sensor`、`espressif/esp_h264`、`espressif/cmake_utilities`、`espressif/nghttp`（包含 nghttp2 及其捆绑的第三方库）。

多数 Espressif 组件采用 Apache-2.0。`espressif/nghttp` 捆绑了 nghttp2（MIT）及其他第三方代码；见该组件内的许可证文件。`espressif/tinyusb` 采用 MIT 许可证。

## 内嵌字体数据

`main/fonts/` 下的 C 字体数组内嵌的字形数据来自：

- **MiSans / MiSans-Regular**（小米）——用于 UI 与时间字体。
- **JetBrains Mono NL**——用于面向终端的 `MI_font_20` 字形集。
- **Font Awesome 5 Free（Solid + Brands）**——图标字形。
- **CodexMono-Hermes**——额外的终端字形。

生成的数组属于固件源码。原始字体文件**不会**在本仓库中再分发。如果你复用或再分发生成的字体数据，请确认你的使用符合每种源字体的许可证。MiSans 和 Font Awesome 有各自的发行条款；JetBrains Mono 以 SIL Open Font License 1.1 分发。

## 拼音数据

`main/ui/pinyin/` 包含项目拼音词典和生成的短语词典。短语词典由多个词表生成；生成来源记录在源码注释中。其中部分来源可能有自己的再分发条款。商业再分发前请审查拼音数据。

## 硬件文档

`doc/Tab5_Schematics_PDF.pdf` 和 `doc/Tab5_Overall_Design_Block_Diagram.pdf` 是 M5Stack Tab5 硬件参考文档，仅供开发参考，仍受 M5Stack 条款约束。

## 发布包资源

`release/` 是此前发布的发布包。

- `release/images/` 包含为本项目制作的产品照片和 UI 截图。
- `release/test_images_with_EXIF/` 包含用于测试 EXIF 显示的第三方示例照片，不在项目源码许可证的覆盖范围内。
- 预编译固件镜像内嵌上述第三方组件。

## 免责声明

固件与文档按“原样”提供，不附带任何形式的担保。适用免责声明见各许可证文本。
