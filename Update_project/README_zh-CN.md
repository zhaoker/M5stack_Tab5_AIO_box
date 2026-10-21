# Factory Updater（`tab5_factory_updater`）

[English](README.md) | **简体中文**

一个刻意保持精简的维护固件，位于 M5Stack Tab5 的 `factory` 分区。它从 TF 卡安装新的主固件，并决定设备下一次启动哪个应用。

这是与主应用独立的 ESP-IDF 项目。它不包含 LVGL、Wi-Fi、网络协议栈，也不包含中文 UI 资源。

## 为什么需要这个项目

固件被拆成两个独立构建的应用是刻意设计，并非历史布局遗留：

| 镜像 | 分区 | 大小 | 职责 |
| --- | --- | --- | --- |
| 主应用 | `main_app`（`ota_0`） | 15.38 MB | SSH 客户端、文件管理器、Wi-Fi、LVGL UI、OTA 下载客户端 |
| Factory updater | `factory` | 512 KB | 校验并安装 `update.bin`，选择下一个启动分区 |

这种布局是为了同时保住**未来固件扩展**和**OTA 能力**：

- **为功能留出最大空间。** 主应用占用几乎全部 16 MB Flash。新的 UI 模块、字体、协议支持和媒体功能可以继续增长，不必和 updater 争抢空间。
- **安装器独立于被安装的应用。** 写入 `main_app` 的代码从不运行在 `main_app` 中。即使主固件损坏或只写入了一半，仍可通过同一个 updater 替换，因为它运行在另一个分区。
- **updater 保持可替换但足够小。** 主应用可以下载新的 updater 镜像、校验大小与 SHA-256，并且只在用户明确确认后写入 `factory` 分区。保持镜像小可以降低该操作的风险和耗时。
- **OTA 仍是标准 ESP-IDF 流程。** `main_app` 的标签是 `main_app`，但子类型保持 `ota_0`，因此 `esp_ota_*` 分区查找、`otadata` 启动选择和标准 bootloader 都能继续工作。
- **外部 Launcher 仍可使用。** `main_app` 是位于 `0x20000` 的第一个应用分区。从整包镜像中提取第一个应用的工具（例如 M5Launcher 类启动器）仍能找到 SSH 客户端。
- **恢复入口稳定。** `factory` 分区是独立的应用子类型。当主应用缺失或有待处理的 TF 卡更新时，由 updater 决定启动哪个应用。

代价是：任何影响安装流程的固件改动，都必须在两个项目之间协同修改。下面的规则用于控制这一成本。

## 设计规则

- 新的用户可见功能放在主应用，不要放在这里。
- 保持固件不超过 512 KB 的 `factory` 分区。它以 `MINIMAL_BUILD` 构建，不得引入 LVGL、Wi-Fi 或其他大型组件。
- updater 的输出保持纯英文且极其简单：串口日志、进度条和纯色错误屏。
- 根目录 `partitions.csv` 与 `Update_project/partitions.csv` 必须完全一致。
- 不要只在单个项目里修改升级协议。见[升级协议](#升级协议)。

## 启动与升级流程

```text
主应用（OTA 页面）
  检查清单
  下载 update.bin 到 /sdcard/update
  校验大小与 SHA-256
  写入 request.json
  用户确认安装
  esp_ota_set_boot_partition(factory)
  重启
        |
        v
Factory updater（本项目）
  挂载 TF 卡
  存在 request.json？
    否 -> 校验 main_app 镜像，设置启动到 main_app，重启
    是 -> 校验 update.bin，写入 main_app，删除 request.json，
          设置启动到 main_app，重启
        |
        v
主应用（新版本）
```

updater 从不写入当前正在运行的分区。如果 TF 卡不可用或没有升级请求，它会启动现有的主应用。

## TF 卡布局

```text
/sdcard/update/
├── request.json     # 升级请求，由主应用写入
├── update.bin       # 待安装的主固件镜像
├── progress.json    # 实时安装进度，由 updater 写入
├── history.jsonl    # 追加式安装历史
└── last_error.txt   # 最近一次失败原因（如有）
```

updater 会在需要时创建 `/sdcard/update`。`progress.json`、`history.jsonl` 和 `last_error.txt` 是诊断输出，可以安全删除。

### 升级协议

`request.json` 由主应用写入，格式如下：

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

当前的 updater 以 `request.json` 的**存在**作为触发条件，始终读取 `/sdcard/update/update.bin`、写入 `main_app`，并在安装成功后删除请求文件。JSON 字段用于描述和向前兼容。updater 会重新校验镜像本身，而不是信任请求内容。

如需修改协议，请同时更新 `main/ota_update/ota_update_tasks.inc` 中的 `write_request_json()` 和本 updater。

### 镜像校验

在写入任何内容之前，updater 会检查：

- 文件存在、非空，且能放入 `main_app` 分区。
- ESP 镜像魔数有效。
- 能读取内嵌的应用描述符。
- `project_name` 必须正好是 `M5stack_tab5_ssh_client`，因此为其他开发板或项目构建的镜像会被拒绝。
- 目标分区不是当前正在运行的分区。
- 写入完成后，`esp_ota_end()` 接受完整镜像。

写入成功后，它会删除 `request.json`、将 `main_app` 设为启动分区、关闭背光并重启。

### 失败行为

失败时，updater 会：

- 停止，而不是启动可疑镜像；
- 如果显示已经初始化，显示纯红色错误屏；
- 写入 `/sdcard/update/last_error.txt`；
- 向 `history.jsonl` 追加一条 `install_failed` 记录；
- 保留 `request.json` 和 `update.bin`。

复位后会重试同一个安装包。要停止重试，请从 TF 卡删除 `request.json`，或用有效镜像替换 `update.bin`。如果主应用本身无效且无法通过 TF 卡更新，updater 会拒绝启动它并停留在维护流程；此时请通过 USB 或 Launcher 重新刷写。

## 状态输出

updater 仅在存在升级请求时初始化显示。安装期间，它直接通过 LCD 面板驱动绘制全屏进度条，不使用 LVGL。如果显示初始化后安装失败，屏幕会变为纯红色并停止。没有升级请求的启动是静默的，不会初始化显示。

串口控制台刻意保留详细日志。在 `115200` 波特率下，它会打印分区表转储、检测到的项目和版本、写入进度以及任何失败原因。这是现场调试的主要通道。

## 构建

使用与主项目相同的 ESP-IDF 版本（v5.5.4）：

```sh
cd Update_project
idf.py -B build_updater build
```

输出：

```text
Update_project/build_updater/tab5_factory_updater.bin
```

设置 `TAB5_UPDATER_VER` 可覆盖写入镜像的版本号；`TAB5_PROJECT_VER` 作为回退。

构建由 `sdkconfig.defaults` 配置，并启用 `MINIMAL_BUILD`。不要向本项目添加主应用的 LVGL 或 Wi-Fi 依赖。

## 刷写

主项目构建不会生成 factory updater，在仓库根目录执行 `idf.py flash` 也不会写入 `factory` 分区。请单独刷写。

手动刷写到分区偏移：

```powershell
python -m esptool --chip esp32p4 -p COMx -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m 0xF80000 Update_project\build_updater\tab5_factory_updater.bin
```

在 Windows 上，辅助脚本会使用从 `partitions.csv` 读取的偏移，一起刷写 bootloader、分区表、OTA 数据和 updater：

```powershell
powershell -ExecutionPolicy Bypass -File .\Update_project\flash_factory_updater_layout.ps1 -Port COMx -UpdaterOnly
```

不加 `-UpdaterOnly` 时，还会把 `build/M5stack_tab5_ssh_client.bin` 刷入 `main_app` 偏移。添加 `-EraseFlash` 可刷成干净设备。

空白设备请先刷完整发布镜像；见 [BUILDING_zh-CN.md](../BUILDING_zh-CN.md)。

## 手动准备 TF 升级包

如果不想使用 OTA 服务器进行测试，可先构建主应用，再复制到 TF 卡并创建请求文件：

```powershell
powershell -ExecutionPolicy Bypass -File .\Update_project\prepare_tf_update_package.ps1 -TfRoot E:\
```

这会写入：

```text
E:\update\update.bin
E:\update\request.json
```

插入 TF 卡，让设备启动到 updater，安装会自动开始。

## 分区参考

| 名称 | 类型 | 子类型 | 偏移 | 大小 |
| --- | --- | --- | --- | --- |
| `nvs` | data | nvs | `0x9000` | `0x6000` |
| `otadata` | data | ota | `0xF000` | `0x2000` |
| `phy_init` | data | phy | `0x11000` | `0x1000` |
| `main_app` | app | `ota_0` | `0x20000` | `0xF60000` |
| `factory` | app | factory | `0xF80000` | `0x80000` |

`main_app` 是标签，不是子类型。其子类型必须保持 `ota_0`，供 ESP-IDF 启动选择使用。两份 `partitions.csv` 必须一致。

## 主应用如何更新此 updater

主应用的 OTA 页面将此镜像视为独立组件：

1. 从 OTA 清单读取 `updater` 对象。
2. 要求提供版本、URL、大小和 SHA-256。
3. 拒绝大于 `factory` 分区的镜像。
4. 下载镜像到 RAM，校验大小和 SHA-256。
5. 仅在用户确认后擦除并写入 `factory` 分区。
6. 回读新的应用描述符并显示已安装版本。

新 updater 会在设备下次启动 factory 分区时生效，而不是立即生效。主应用 UI 将该组件标为“UPLOAD 固件”；它就是这里构建的 `tab5_factory_updater`。

## 相关文档

- [BUILDING_zh-CN.md](../BUILDING_zh-CN.md)——完整构建、分区与刷写流程。
- [DEVELOPMENT.md](../DEVELOPMENT.md)——主应用架构与维护说明（简体中文）。
- [README_zh-CN.md](../README_zh-CN.md)——项目概览与许可证状态。
- [README.md](README.md)——本文档的英文版本。
