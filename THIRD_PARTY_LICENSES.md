# Third-Party Licenses and Notices

**English** | [简体中文](THIRD_PARTY_LICENSES_zh-CN.md)

This repository contains the project's own source code plus vendored third-party components, generated font data, and a prebuilt release bundle. The project's own source code is licensed under the [Apache License 2.0](LICENSE); see [NOTICE](NOTICE) for attribution. The following components remain under their own licenses.

## Vendored Components

| Component | Version | License | License file |
| --- | --- | --- | --- |
| LVGL | 9.2.2 | MIT | `components/lvgl/LICENCE.txt` |
| esp_lvgl_port | 2.5.0 | Apache-2.0 | `components/espressif__esp_lvgl_port/license.txt` |
| M5Stack Tab5 BSP | local | Apache-2.0 | `components/m5stack_tab5/LICENSE` |
| libssh_esp32 / libssh | local port | LGPL-2.1; some parts BSD-2-Clause | `components/libssh_esp32/COPYING`, `components/libssh_esp32/BSD` |
| FatFs | R0.15 patch2 | FatFs license (BSD-style) | header of `components/fatfs/src/ff.c` |
| esp_lcd_st7121 | local | Apache-2.0 | SPDX header in `components/esp_lcd_st7121/esp_lcd_st7121.c` |

`libssh_esp32` is distributed under the LGPL-2.1. The complete corresponding source is included in this repository, so the firmware can be rebuilt with a modified or relinked libssh. Some libssh files are under the BSD-2-Clause license; see `components/libssh_esp32/BSD`.

## Managed Components Fetched During Build

The ESP-IDF component manager downloads the following components into `managed_components/` on the first build. They are not committed to this repository. Each component carries its own license file in its downloaded directory.

`espressif/esp_hosted`, `espressif/esp_wifi_remote`, `espressif/eppp_link`, `espressif/esp_serial_slave_link`, `espressif/esp_tinyusb`, `espressif/usb_host_msc`, `espressif/usb_host_hid`, `espressif/usb_host_uvc`, `espressif/esp_codec_dev`, `espressif/esp_lcd_ili9881c`, `espressif/esp_lcd_st7703`, `espressif/esp_lcd_touch`, `espressif/esp_lcd_touch_gt911`, `espressif/esp_lcd_touch_st7123`, `espressif/esp_cam_sensor`, `espressif/esp_h264`, `espressif/cmake_utilities`, `espressif/nghttp` (includes nghttp2 and its bundled third-party libraries).

Most Espressif components are Apache-2.0. `espressif/nghttp` bundles nghttp2 (MIT) and additional third-party code; see the license files inside that component. `espressif/tinyusb` is MIT-licensed.

## Embedded Font Data

The C font arrays under `main/fonts/` embed glyph data generated from:

- **MiSans / MiSans-Regular** (Xiaomi) — used by the UI and time fonts.
- **JetBrains Mono NL** — used for the terminal-oriented `MI_font_20` glyph set.
- **Font Awesome 5 Free (Solid + Brands)** — icon glyphs.
- **CodexMono-Hermes** — additional terminal glyphs.

The generated arrays are part of the firmware source. The original font files are **not** redistributed in this repository. If you reuse or redistribute the generated font data, verify that your use complies with the license of each source font. MiSans and Font Awesome have their own distribution terms; JetBrains Mono is distributed under the SIL Open Font License 1.1.

## Pinyin Data

`main/ui/pinyin/` contains a project pinyin dictionary and a generated phrase dictionary. The phrase dictionary was generated from multiple word lists; the generation sources are noted in the source comments. Some of those sources may have their own redistribution terms. Review the pinyin data before commercial redistribution.

## Hardware Documentation

`doc/Tab5_Schematics_PDF.pdf` and `doc/Tab5_Overall_Design_Block_Diagram.pdf` are M5Stack Tab5 hardware reference documents. They are included for development reference and remain subject to M5Stack's terms.

## Release Bundle Assets

`release/` is the previously published release bundle.

- `release/images/` contains product photos and UI screenshots created for this project.
- `release/test_images_with_EXIF/` contains third-party sample photographs used to test EXIF display. They are not covered by the project's source license.
- The prebuilt firmware images embed the third-party components listed above.

## No Warranty

The firmware and documentation are provided without warranty of any kind. See the individual license texts for the applicable disclaimers.
