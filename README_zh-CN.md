# M5Stack Tab5 AIO Box

[English](README.md) | **简体中文** | [繁體中文](README_zh-TW.md)

> 基于 ESP32-P4 + M5Stack Tab5 硬件平台的便携式 SSH 终端、文件管理器、媒体查看器和 OTA 维护工具。

## 快速使用

如果只想快速上手，请从这里开始：

**[简体中文快速使用手册](QUICK_START_GUIDE_zh-CN.md)**  
[English Quick Start Guide](QUICK_START_GUIDE.md) | [繁體中文快速使用手冊](QUICK_START_GUIDE_zh-TW.md)

不同语言版本对应不同快速手册。简体中文手册会包含中文拼音输入法、实体键盘和软键盘的详细操作说明。

---

## 系统语言

首次进入系统时可以选择系统语言，后续可以在 **系统设置 > 系统** 中随时更改。

当前支持：

- English
- 简体中文
- 繁體中文

![语言选择](images/tab5.jpg)

---

## 注意事项

1、当设备开启WIFI使用在线文件管理功能上传文件时候耗电巨大，接近11W功耗，USB刷新固件之后请使用满足5V 2A的电源适配器供电。
2、由于ESP32-P4 + C6 的架构性已知缺陷（入站TCP数据（上传方向）的 SDIO flow control 缺陷）在线文件管理上传多个大文件极不稳定，暂时等待社区那边新的解决方案。

## 产品预览

![实物照片](images/tab5_1.jpg)

设备可配合 Tab5 触摸屏、可选实体键盘、TF 卡存储、WiFi 网络和内置 SSH 终端界面使用。

---

## 核心亮点

### SSH 终端客户端

- 支持保存多个 SSH 服务器，短按服务器卡片快速连接
- 长按服务器卡片可编辑或删除
- 支持 IPv4/IPv6 网络状态显示
- 全屏终端显示，触摸热区呼出状态栏、控制栏和软键盘
- 支持可选实体键盘，覆盖常用终端按键

![SSH 终端](images/cn_tab5_2.jpg)

### 中文输入与键盘

- 中文系统默认支持拼音输入
- 拼音模式可通过触摸候选字或数字键选字
- 英文模式直接发送字符到远端终端
- 实体键盘自动适配，检测到键盘后可隐藏软键盘

![终端连接](images/cn_tab5_4.jpg)

### TF 卡文件管理器

- 本地浏览、复制、剪切、粘贴、重命名、删除 TF 卡文件
- 支持全选、复制、剪切、删除等批量操作
- 支持图像查看、文本查看、EXIF 信息显示、MP3/FLAC 音乐播放
- WiFi 环境下可开启在线文件管理器，通过浏览器访问设备文件

![文件管理器](images/cn_tab5_3.jpg)
![图像浏览](images/cn_tab5_7.jpg)
### 音乐播放器

- 从 TF 卡播放 MP3/FLAC 音乐
- 支持后台音乐条
- 支持收藏夹
- 支持读取歌曲内嵌封面并显示

![音乐播放器](images/cn_tab5_8.jpg)

### OTA 固件升级

- 在设置页检测主程序和 UPLOAD 固件更新
- 下载固件包到 TF 卡
- 安装前进行版本和文件校验

![固件升级](images/cn_tab5_6.jpg)

### 系统工具

- WiFi 管理和已保存网络列表
- 电池、USB-C、充电状态显示
- 三指滑动截屏，保存到 TF 卡 `/ScreenShots`
- 支持加密备份和非加密备份
- 支持语言、时区、顶部后台音乐条、显示等设置
- 支持设置待机屏保，屏保显示时候长按可移动组件

![系统设置](images/cn_tab5_1.jpg)
![待机屏保](images/tab5_2.jpg)
---

## 硬件平台

- **芯片**：ESP32-P4
- **设备**：M5Stack Tab5
- **存储**：16 MB Flash + 32 MB PSRAM
- **接口**：TF 卡、USB-C、可选实体键盘
- **屏幕**：720 x 1280 MIPI DSI 触摸屏

---

## 目录结构

```text
├── flash-at-0x0/              # 完整固件，使用 esptool 从 0x0 地址刷入
├── images/                    # 产品截图
├── exif-test-images/          # 含 EXIF 信息的测试图片
├── QUICK_START_GUIDE.md       # 英文快速使用手册
├── QUICK_START_GUIDE_zh-CN.md # 简体中文快速使用手册
├── QUICK_START_GUIDE_zh-TW.md # 繁體中文快速使用手冊
├── README.md                  # GitHub 默认英文项目说明
├── README_en.md               # 英文项目说明镜像
├── README_zh-CN.md            # 简体中文项目说明
└── README_zh-TW.md            # 繁體中文專案說明
```

---

## 隐私说明

SSH 服务器凭据使用 AES-256-GCM 加密存储。配置备份可根据迁移场景选择加密备份或非加密备份。

除 OTA 所需的必要请求外，固件不会向外部服务发送 SSH、WiFi 或文件隐私数据。

## 许可证

本项目使用 libssh 库（LGPL v2.1）。
