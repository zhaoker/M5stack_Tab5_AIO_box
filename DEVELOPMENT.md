# ESP32-P4 M5Stack Tab5 SSH Client 开发手册

这份文档不是普通的项目简介，而是给后续开发者和其他 AI 快速接手用的详细备忘录。目标是让读者在没有上下文的情况下，能快速理解这个项目的运行框架、模块边界、关键流程、重要参数、常见修改入口和容易踩坑的地方。

项目定位：在 `M5Stack Tab5` 上运行一个触摸式 SSH 客户端。主控是 `ESP32-P4`，网络由板载 `ESP32-C6` 通过 `esp_hosted + esp_wifi_remote` 提供。UI 使用 `LVGL`，SSH 使用本地组件 `libssh_esp32`，终端输出由项目内 ANSI/VT100 解析器和 Canvas 渲染器完成。

当前工程不是演示 demo，而是一个已经具备完整链路的嵌入式应用：

- Tab5 LCD、触摸、背光、LVGL 初始化。
- ESP32-P4 通过 ESP32-C6 远程 Wi-Fi 联网。
- Wi-Fi 扫描、连接、断开、多凭据保存、删除、自动连接。
- 服务器配置保存、编辑、删除、图标选择。
- SSH 密码认证、PTY Shell、自动命令。
- ANSI/VT100 终端解析、UTF-8、宽字符、颜色、备用屏、滚动历史。
- LVGL Canvas 终端渲染、特殊 TUI 字符软件绘制。
- 中文 UI 字体、拼音输入法、自定义拼音词库。

---

## 1. 一句话架构

启动后 `app_main()` 初始化 NVS、显示和主 UI，然后初始化 Wi-Fi；主页 `ui_server_list` 管理服务器和 Wi-Fi 入口；点击服务器后 `ui_terminal` 打开终端页并创建 SSH 连接任务；SSH 后台接收任务把字节流喂给 `ansi_parser`；LVGL 定时器调用 `term_renderer` 把终端状态画到 Canvas。

主链路可以记成：

```text
app_main
  -> ui_server_list_create
  -> wifi_manager_init / wifi_manager_auto_connect
  -> 用户点击服务器卡片
  -> ui_terminal_open
  -> ssh_connect_task
  -> ssh_client_connect
  -> ssh_recv_task
  -> on_ssh_data
  -> ansi_parser_feed
  -> render_timer_cb
  -> term_renderer_refresh
```

---

## 2. 快速定位地图

最常读的文件：

```text
main/main.c                         应用入口，NVS/LVGL/主页/Wi-Fi 启动顺序
main/app_config.h                   全局字体、终端热区和标题栏参数
main/wifi_manager.c/.h              Wi-Fi 状态机、扫描、多凭据 NVS、IPv4/IPv6 文本
main/storage/server_config.c/.h     服务器配置 NVS 存储，含旧结构兼容
main/ssh/ssh_client.c/.h            libssh 会话、PTY、接收任务、事件回调
main/terminal/ansi_parser.c/.h      ANSI/VT100 解析器，维护 term_state_t
main/terminal/term_renderer.c/.h    LVGL Canvas 终端渲染
main/terminal/term_special_chars.c  box drawing/block/braille 等特殊字符绘制
main/ui/ui_server_list.c/.h         首页、服务器卡片、服务器编辑弹窗、图标选择
main/ui/ui_wifi.c/.h                Wi-Fi 管理浮层、扫描列表、手动连接区域
main/ui/ui_terminal.c/.h            终端页面、键盘、拼音、热区、SSH 数据接入
main/ui/theme.hpp                   首页/终端等通用主题 token
main/ui/pinyin/pinyin_dict_custom.c/.h     自定义拼音候选词库
main/fonts/MI_font_20.h/.c          当前中文 UI 字体
components/m5stack_tab5             本地 Tab5 BSP
components/libssh_esp32             本地 libssh 移植组件
components/lvgl                     本地 LVGL 组件
```

构建配置入口：

```text
CMakeLists.txt              顶层工程
main/CMakeLists.txt         应用源文件、include、依赖、强制 include app_config.h
sdkconfig.defaults          默认目标、PSRAM、ESP Hosted、LVGL 字体/输入法等
partitions.csv              NVS、factory app、SPIFFS 预留区
dependencies.lock           当前组件解析版本
```

硬件资料：

```text
doc/Tab5_Schematics_PDF.pdf
doc/Tab5_Overall_Design_Block_Diagram.pdf
```

---

## 3. 硬件和软件基线

硬件：

| 项目 | 当前信息 |
| --- | --- |
| 开发板 | M5Stack Tab5 |
| 主控 | ESP32-P4 |
| 无线 | ESP32-C6，通过 SDIO 由 P4 侧驱动 |
| LCD | ILI9881C，物理分辨率 720 x 1280，MIPI DSI |
| 触摸 | GT911 |
| PSRAM | 已启用，图形缓冲和终端 Canvas 强依赖 |

软件：

| 项目 | 当前信息 |
| --- | --- |
| ESP-IDF | 5.5.4，见 `dependencies.lock` |
| target | `esp32p4` |
| LVGL | 本地组件 `components/lvgl`，锁定版本显示为 `9.2.2` |
| esp_lvgl_port | 本地组件路径 `components/espressif__esp_lvgl_port`，锁定版本 `2.5.0` |
| BSP | 本地 `components/m5stack_tab5` |
| SSH | 本地 `components/libssh_esp32` |
| UI 字体 | `MI_font_20` |

注意：旧文档中可能出现过其他 LVGL/esp_lvgl_port 版本描述，以当前 `dependencies.lock` 为准。

---

## 4. 启动流程

入口在 `main/main.c` 的 `app_main()`。

当前启动顺序：

1. `nvs_flash_init()`。
2. 如果 NVS 分区版本不匹配或空间异常，执行 `nvs_flash_erase()` 后重新初始化。
3. `esp_event_loop_create_default()`。
4. `server_config_init()`，读服务器数量。
5. 配置 LVGL port 任务栈：`lvgl_port_cfg.task_stack = 12288`。
6. `bsp_display_start_with_config()` 初始化显示和 LVGL。
7. 显示缓冲配置：
   - `buffer_size = BSP_LCD_H_RES * BSP_LCD_V_RES`
   - `double_buffer = true`
   - `buff_dma = true`
   - `buff_spiram = true`
   - `sw_rotate = true`
8. `lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_90)`，逻辑横屏。
9. `bsp_display_brightness_set(45)`。
10. 加 LVGL 锁，清屏、设置根屏背景和默认字体。
11. `ui_server_list_create(lv_scr_act())` 创建主页。
12. 解 LVGL 锁。
13. `wifi_manager_init()`。注意现在 Wi-Fi 初始化放在显示之后，注释里说明是为了避免占用显示初始化依赖的内部 RAM。
14. `wifi_manager_auto_connect()`，只尝试已保存凭据；没有凭据则等待手动连接。
15. `while (true) vTaskDelay(10ms)` 保持主任务存活。

LVGL 不是线程安全的。主动创建 UI 时需要 `bsp_display_lock()/bsp_display_unlock()`。运行期很多刷新由 LVGL timer 回调触发，已经在 LVGL 上下文内。

---

## 5. 目录结构和模块职责

```text
.
|-- main/
|   |-- main.c
|   |-- app_config.h
|   |-- wifi_manager.c/.h
|   |-- storage/
|   |   `-- server_config.c/.h
|   |-- ssh/
|   |   `-- ssh_client.c/.h
|   |-- terminal/
|   |   |-- ansi_parser.c/.h
|   |   |-- term_renderer.c/.h
|   |   `-- term_special_chars.c/.h
|   |-- ui/
|   |   |-- ui_server_list.c/.h
|   |   |-- ui_wifi.c/.h
|   |   |-- ui_terminal.c/.h
|   |   |-- theme.hpp
|   |   `-- pinyin_dict_custom.c/.h
|   `-- fonts/
|       `-- MI_font_20.h/.c
|-- components/
|   |-- m5stack_tab5/
|   |-- libssh_esp32/
|   `-- lvgl/
|-- managed_components/
|-- sdkconfig.defaults
|-- partitions.csv
`-- dependencies.lock
```

模块边界：

- `wifi_manager` 不直接操作 UI，只提供状态、扫描、连接、凭据接口。
- `server_config` 不关心 UI，只保存固定结构体。
- `ssh_client` 不解析终端，只把 SSH 字节通过回调交出去。
- `ansi_parser` 不画图，只维护 `term_state_t`。
- `term_renderer` 不处理 SSH，只把 `term_state_t` 画到 LVGL Canvas。
- `ui_terminal` 是 SSH、解析器、渲染器、键盘输入之间的胶水。
- `ui_server_list` 是首页和服务器表单。
- `ui_wifi` 是 Wi-Fi 浮层。

---

## 6. 构建和烧录

进入 ESP-IDF 环境后：

```powershell
idf.py set-target esp32p4
idf.py reconfigure
idf.py build
idf.py flash monitor
```

只烧录：

```powershell
idf.py flash
```

当前聊天环境里的普通 PowerShell 未必已经加载 ESP-IDF，所以如果出现 `idf.py : The term 'idf.py' is not recognized`，需要先运行 ESP-IDF 的导出脚本或使用 ESP-IDF PowerShell。

---

## 7. 关键配置参数

`sdkconfig.defaults` 里最关键的设置：

```text
CONFIG_IDF_TARGET="esp32p4"
CONFIG_ESP32P4_REV_MIN_100=y
CONFIG_ESPTOOLPY_FLASHMODE_QIO=y
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
CONFIG_COMPILER_OPTIMIZATION_PERF=y
CONFIG_SPIRAM=y
CONFIG_SPIRAM_SPEED_200M=y
CONFIG_SPIRAM_XIP_FROM_PSRAM=y
CONFIG_CACHE_L2_CACHE_256KB=y
CONFIG_CACHE_L2_CACHE_LINE_128B=y
CONFIG_FREERTOS_HZ=1000
CONFIG_LV_COLOR_DEPTH_16=y
CONFIG_LV_USE_IME_PINYIN=y
CONFIG_LV_IME_PINYIN_CAND_TEXT_NUM=15
CONFIG_LV_USE_CLIB_MALLOC=y
```

ESP Hosted / SDIO Wi-Fi：

```text
CONFIG_ESP_HOSTED_ENABLED=y
CONFIG_ESP_HOSTED_SDIO_HOST_INTERFACE=y
CONFIG_ESP_HOSTED_IDF_SLAVE_TARGET="esp32c6"
CONFIG_SLAVE_IDF_TARGET_ESP32C6=y
CONFIG_ESP_HOSTED_SDIO_RESET_ACTIVE_LOW=y
CONFIG_ESP_HOSTED_SDIO_4_BIT_BUS=y
CONFIG_ESP_HOSTED_SDIO_BUS_WIDTH=4
CONFIG_ESP_HOSTED_SDIO_CLOCK_FREQ_KHZ=40000
CONFIG_ESP_HOSTED_SDIO_GPIO_RESET_SLAVE=15
CONFIG_ESP_HOSTED_SDIO_PIN_CMD=13
CONFIG_ESP_HOSTED_SDIO_PIN_CLK=12
CONFIG_ESP_HOSTED_SDIO_PIN_D0=11
CONFIG_ESP_HOSTED_SDIO_PIN_D1=10
CONFIG_ESP_HOSTED_SDIO_PIN_D2=9
CONFIG_ESP_HOSTED_SDIO_PIN_D3=8
```

LVGL 内置字体开启：

```text
CONFIG_LV_FONT_MONTSERRAT_10=y
CONFIG_LV_FONT_MONTSERRAT_12=y
CONFIG_LV_FONT_MONTSERRAT_14=y
CONFIG_LV_FONT_MONTSERRAT_16=y
CONFIG_LV_FONT_MONTSERRAT_18=y
CONFIG_LV_FONT_MONTSERRAT_20=y
CONFIG_LV_FONT_MONTSERRAT_22=y
CONFIG_LV_FONT_MONTSERRAT_24=y
CONFIG_LV_FONT_MONTSERRAT_28=y
CONFIG_LV_FONT_MONTSERRAT_32=y
```

`main/app_config.h`：

```c
#define APP_FONT MI_font_20
#define APP_IME_CAND_FONT MI_font_20
#define APP_TERM_STATUSBAR_H      50
#define APP_TERM_HOTZONE_TOP_H    100
#define APP_TERM_HOTZONE_LEFT_W   200
#define APP_TERM_HOTZONE_RIGHT_W  200
#define APP_TERM_CONTROL_BAR_H    50
#define APP_TERM_TITLE_FONT (&APP_FONT)
```

键盘字体由 `app_keyboard_font()` 选择，优先 `lv_font_montserrat_32`，再降级到 30、28、默认字体。这样做是为了让 LVGL 内置键盘上的英文、数字、符号足够清晰。

`main/CMakeLists.txt` 通过：

```cmake
target_compile_options(${COMPONENT_LIB} PRIVATE -include app_config.h)
```

强制所有编译单元自动包含 `app_config.h`。这会影响字体源文件、UI 源文件和终端渲染源文件。改字体或全局参数时要记住这里的强制 include。

---

## 8. 分区和持久化

`partitions.csv`：

| 分区 | 类型 | 偏移 | 大小 | 用途 |
| --- | --- | --- | --- | --- |
| `nvs` | data/nvs | `0x9000` | `0x6000` | Wi-Fi 凭据、服务器配置 |
| `phy_init` | data/phy | `0xf000` | `0x1000` | PHY 初始化 |
| `factory` | app/factory | `0x10000` | `10M` | 主固件 |
| `storage` | data/spiffs | 自动 | `2M` | 预留 SPIFFS |

当前主应用逻辑主要使用 NVS。`storage` SPIFFS 已预留，适合未来放：

- known_hosts 文件。
- 日志。
- 配置导入导出。
- 私钥文件。
- 离线资源。

安全提醒：

- 当前 Wi-Fi 密码明文存在 NVS。
- 当前服务器密码明文存在 NVS。
- 当前 known_hosts 行为依赖 libssh 默认路径/行为，未做专门 UI 管理。

---

## 9. Wi-Fi 管理

入口：`main/wifi_manager.c/.h`。

### 9.1 常量

```c
#define WIFI_NVS_NAMESPACE "wifi_creds"
#define WIFI_MAX_SSID_LEN  64
#define WIFI_MAX_PASS_LEN  64
#define WIFI_MAX_SAVED_NETWORKS 8
#define WIFI_MAX_SCAN_RESULTS   16
```

内部 NVS key：

```c
#define WIFI_STORE_VERSION       1
#define WIFI_NVS_KEY_NETWORKS    "networks"
#define WIFI_NVS_KEY_LAST_SSID   "last_ssid"
#define WIFI_NVS_KEY_LEGACY_SSID "ssid"
#define WIFI_NVS_KEY_LEGACY_PASS "pass"
```

说明：

- 新格式使用一个 `wifi_store_t` blob 保存最多 8 个网络。
- `last_ssid` 记录最后连接的网络，用于自动连接优先级。
- 代码保留旧格式 `ssid/pass` 迁移逻辑，方便兼容早期版本。

### 9.2 状态机

```c
typedef enum {
    WIFI_STATE_IDLE,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_DISCONNECTED,
    WIFI_STATE_FAILED,
} wifi_state_t;
```

内部关键状态：

- `s_initialized`：Wi-Fi 管理器是否初始化。
- `s_netif_created`：默认 STA netif 是否创建。
- `s_wifi_unavailable`：Wi-Fi 初始化失败后标记不可用。
- `s_manual_disconnect`：用户主动断开，避免自动重连逻辑误判。
- `s_wifi_started`：esp_wifi 是否 start。
- `s_connect_requested`：是否有主动连接请求。
- `s_retry_num`：断线重试计数。

重试次数：

```c
#define WIFI_MAX_RETRY 5
```

事件 bit：

```c
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
```

### 9.3 初始化流程

`wifi_manager_init()` 做的事：

1. 通过 BSP 给 ESP32-C6 Wi-Fi 模块上电：`bsp_feature_enable(BSP_FEATURE_WIFI, true)`。
2. 初始化 `esp_netif`。
3. 创建默认 STA：`esp_netif_create_default_wifi_sta()`。
4. 初始化 `esp_wifi`。
5. 注册 Wi-Fi 和 IP 事件回调。
6. 设置 STA 模式并启动 Wi-Fi。

ESP32-P4 自己没有 Wi-Fi 射频，所以这一步依赖 `esp_hosted + esp_wifi_remote`。P4 侧代码仍然调用标准 `esp_wifi_*` API。

### 9.4 连接行为

核心函数：

```c
esp_err_t wifi_manager_connect(const char *ssid, const char *password, bool save_creds);
```

连接配置重点：

```c
wifi_config.sta.threshold.authmode = (password && password[0]) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
wifi_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
wifi_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
```

这表示：

- 有密码时要求 WPA2-PSK 或更高。
- 无密码时允许开放网络。
- 扫描全信道并按信号排序连接。

保存凭据：

- `save_creds = true` 时会调用保存逻辑。
- 同 SSID 会更新密码。
- 新 SSID 会追加到 `wifi_store_t.items`。
- 超过 `WIFI_MAX_SAVED_NETWORKS` 需要看保存逻辑如何处理，维护时应优先检查边界。

自动连接：

```c
esp_err_t wifi_manager_auto_connect(void);
```

逻辑重点：

- 优先读取 `last_ssid`。
- 找到对应保存网络后连接。
- 没有保存凭据返回 `ESP_ERR_NOT_FOUND`，主程序等待用户手动连接。

### 9.5 扫描和当前 IP

扫描接口：

```c
esp_err_t wifi_manager_scan_networks(wifi_scan_result_t *out_results, size_t max_items, size_t *out_count);
```

扫描结果结构：

```c
typedef struct {
    char ssid[WIFI_MAX_SSID_LEN];
    int8_t rssi;
    uint8_t authmode;
    bool saved;
    bool connected;
} wifi_scan_result_t;
```

IP 查询：

```c
bool wifi_manager_has_ipv4(void);
bool wifi_manager_has_ipv6_global(void);
esp_err_t wifi_manager_get_current_ip_text(char *ipv4_buf, size_t ipv4_len,
                                           char *ipv6_buf, size_t ipv6_len);
```

UI 层 `ui_wifi` 和 `ui_server_list` 都会显示 Wi-Fi 状态。`ui_wifi` 当前把 IPv4 和 IPv6 拆成两个 label：

- IPv4 使用 `APP_FONT`，也就是 `MI_font_20`。
- IPv6 使用 `lv_font_montserrat_16`，避免 IPv6 地址太长挤爆布局。

---

## 10. Wi-Fi UI 浮层

入口：`main/ui/ui_wifi.c`。

### 10.1 布局常量

当前关键常量：

```c
#define WIFI_PANEL_MIN_W   900
#define WIFI_PANEL_H       620
#define WIFI_TITLE_H       52
#define WIFI_BUTTON_H      42
#define WIFI_INPUT_H       44
#define WIFI_SECTION_H     180
#define WIFI_LEFT_COL_W    400
#define WIFI_SIDE_PAD      16
#define WIFI_GAP           12
#define WIFI_SMALL_GAP     8
#define WIFI_MANUAL_PAD    14
#define WIFI_FIELD_LABEL_W 58
#define WIFI_FIELD_GAP     10
#define WIFI_FIELD_INPUT_W (WIFI_LEFT_COL_W - (WIFI_MANUAL_PAD * 2) - WIFI_FIELD_LABEL_W - WIFI_FIELD_GAP)
```

左栏宽度是 `400`。手动连接卡片左右 padding 各 `14`，标签宽 `58`，标签和输入框间距 `10`，所以输入框宽度是：

```text
400 - 14*2 - 58 - 10 = 304
```

这几个常量用于保证“SSID 标签 + SSID 输入框”和“密码标签 + 密码输入框”在同一行。如果后续发现又换行，优先检查：

- `WIFI_LEFT_COL_W`
- `WIFI_FIELD_LABEL_W`
- `WIFI_FIELD_GAP`
- `WIFI_FIELD_INPUT_W`
- manual card padding
- 输入框是否又被设成 `LV_PCT(100)` 或默认宽度

### 10.2 主要对象

```c
static lv_obj_t *s_overlay;
static lv_obj_t *s_panel;
static lv_obj_t *s_status_card;
static lv_obj_t *s_manual_title;
static lv_obj_t *s_lbl_state;
static lv_obj_t *s_lbl_ipv4;
static lv_obj_t *s_lbl_ipv6;
static lv_obj_t *s_scan_hint;
static lv_obj_t *s_ta_ssid;
static lv_obj_t *s_ta_pass;
static lv_obj_t *s_kb;
static lv_obj_t *s_saved_list;
static lv_obj_t *s_scan_list;
```

数据缓存：

```c
static wifi_saved_network_t s_saved_networks[WIFI_MAX_SAVED_NETWORKS];
static wifi_scan_result_t s_scan_results[WIFI_MAX_SCAN_RESULTS];
```

### 10.3 输入框光标处理

Wi-Fi 输入框参考了服务器编辑表单的处理方式：

- 默认 `LV_PART_CURSOR` 透明。
- 只有当前 `LV_STATE_FOCUSED` 的 textarea 光标可见。
- 键盘 ready/cancel、连接按钮点击后清掉 focused 状态。

相关函数：

```c
apply_wifi_textarea_cursor_style()
clear_wifi_textarea_focus()
set_wifi_textarea_focus()
on_ta_focus()
on_kb_ready()
```

如果出现两个输入框同时闪光标，优先看 `set_wifi_textarea_focus()` 是否被绕过。

### 10.4 键盘弹出时的布局

`show_wifi_input_layout()` 会隐藏状态卡片和手动连接标题，让输入区域给键盘让位。`restore_wifi_input_layout()` 会恢复。键盘对象是 `s_kb = lv_keyboard_create(s_overlay)`。

### 10.5 扫描列表和保存列表

`ui_wifi.c` 里有多组列表重建函数：

- `refresh_saved_networks()`
- `rebuild_saved_networks_list()`
- `rebuild_scan_list()`
- `scan_and_refresh_networks()`
- `request_scan()`

扫描列表里会标记：

- 已保存。
- 当前连接。
- 信号强度。
- 加密类型。

---

## 11. 服务器配置存储

入口：`main/storage/server_config.c/.h`。

### 11.1 数据结构

```c
#define MAX_SERVERS          10
#define SERVER_NVS_NAMESPACE "srv_cfg"

typedef struct {
    char     name[32];
    char     host[64];
    uint16_t port;
    char     username[32];
    char     password[64];
    char     auto_cmd[128];
    uint8_t  icon_id;
} server_entry_t;
```

NVS 布局：

| key | 类型 | 含义 |
| --- | --- | --- |
| `count` | `uint8_t` | 已保存服务器数量 |
| `srv_0` 到 `srv_9` | blob | `server_entry_t` |

### 11.2 旧结构兼容

`server_config.c` 内部定义了 `legacy_server_entry_t`，比当前结构少 `icon_id`。加载时如果 `nvs_get_blob()` 返回 `ESP_ERR_NVS_INVALID_LENGTH`，会尝试按旧结构读取，再把 `icon_id` 设为默认 `0`。

这意味着：

- 当前固件能读取旧版本保存的服务器条目。
- 如果未来继续改 `server_entry_t` 字段，应该继续保留迁移逻辑，不要直接破坏旧 NVS。

### 11.3 删除行为

`server_config_delete(index)` 会把后续条目前移一位，然后删除最后一个槽位，更新 `count`。这意味着 index 是稳定数组语义，不是持久 ID。UI 长按/点击时传 index，删除后要刷新列表。

---

## 12. 首页和服务器编辑 UI

入口：`main/ui/ui_server_list.c`。

### 12.1 首页职责

主页负责：

- 顶部/底部状态栏。
- Wi-Fi chip 和 IPv4/IPv6 chip。
- 服务器卡片网格。
- 空状态。
- 添加服务器按钮。
- 服务器编辑弹窗。
- 图标选择器。
- 长按操作面板。
- 删除确认。
- 信息弹窗。

### 12.2 卡片布局

关键参数：

```c
GRID_COLUMNS = 3
GRID_GAP = 16
CARD_RATIO_W = 5
CARD_RATIO_H = 3
```

`update_layout_metrics()` 根据屏幕宽度计算卡片尺寸：

- 最小宽度 `300`
- 最大宽度 `360`
- 高度按 `5:3` 比例
- 最小高度 `180`

### 12.3 服务器图标

`s_server_icons[]` 维护图标定义：

```c
typedef struct {
    const char *glyph;
    const char *name;
    uint32_t color_hex;
} server_icon_def_t;
```

其中包含：

- 国家/地区缩写：CN、JP、SG、US、HK、EU。
- LVGL symbols：WiFi、Home、Drive、File、Keyboard 等。
- 系统缩写：WIN、LNX、UBU、DEB、RHL、BSD。

保存到 NVS 的是 `server_entry_t.icon_id`。如果数组顺序改变，会影响旧服务器图标含义。维护时尽量追加，不要重排已有项。

### 12.4 服务器编辑表单

表单对象集中在 `server_form_t s_form`：

```c
name_ta
host_ta
port_ta
user_ta
pass_ta
cmd_ta
keyboard
pinyin_ime
pinyin_cand_panel
```

输入框创建在 `create_textarea_field()` 附近。光标处理已经做过：

- cursor 默认透明。
- focused 时显示。
- 参考点：`LV_PART_CURSOR | LV_STATE_FOCUSED`。

键盘 ready/cancel 会隐藏键盘，表单弹窗会根据键盘高度重新布局，避免遮挡当前输入框。

---

## 13. SSH 客户端

入口：`main/ssh/ssh_client.c/.h`。

### 13.1 设计模型

当前是全局单会话模型：

```c
static ssh_session  s_session;
static ssh_channel  s_channel;
static bool         s_running;
static bool         s_connecting;
static bool         s_cancel_requested;
static TaskHandle_t s_task;
```

这表示：

- 只能同时存在一个 SSH session。
- 只能有一个接收任务。
- 如果要做多标签页，必须先实例化这些全局状态。

### 13.2 参数

```c
#define SSH_RECV_BUF_SIZE    4096
#define SSH_RECV_TIMEOUT_MS  100
#define SSH_TASK_STACK_SZ    12288
#define SSH_TASK_PRIORITY    5
```

`ui_terminal.c` 里连接任务另有：

```c
#define SSH_CONN_TASK_STACK  16384
#define SSH_CONN_TASK_PRIO   4
```

连接任务栈比接收任务大，是为了容纳 libssh 连接/known_hosts 等较深调用。

### 13.3 连接流程

`ssh_client_connect()`：

1. 如果已有连接或正在连接，先调用 `ssh_client_disconnect()`。
2. `libssh_begin()`。
3. 保存 data/event 回调。
4. `ssh_new()`。
5. 设置 host、port、user、log verbosity。
6. `ssh_connect()`。
7. `verify_knownhost()`。
8. `ssh_userauth_password()`。
9. `ssh_channel_new()`。
10. `ssh_channel_open_session()`。
11. `ssh_channel_request_pty_size(s_channel, "xterm-256color", cols, rows)`。
12. `ssh_channel_request_shell()`。
13. 如 `auto_cmd` 非空，延迟 `200 ms`，发送 `auto_cmd + "\n"`。
14. 创建 `ssh_recv_task`。
15. 触发 `SSH_EVT_CONNECTED`。

### 13.4 known_hosts 行为

`verify_knownhost()` 当前行为：

- `SSH_KNOWN_HOSTS_OK`：通过。
- `SSH_KNOWN_HOSTS_NOT_FOUND` / `UNKNOWN`：自动接受并 `ssh_session_update_known_hosts()`。
- `SSH_KNOWN_HOSTS_CHANGED` / `OTHER` / `ERROR`：失败。

这不是严格安全交互。产品化建议：

- 首次连接弹窗显示 fingerprint。
- 用户确认后再写 known_hosts。
- known_hosts 存储路径最好迁到 SPIFFS 并可管理。

### 13.5 认证限制

当前只支持密码认证：

```c
ssh_userauth_password(s_session, NULL, cfg->password)
```

不支持：

- 公钥认证。
- agent。
- keyboard-interactive。
- MFA。

---

## 14. 终端页面

入口：`main/ui/ui_terminal.c`。

### 14.1 页面模型

终端页对象：

```c
s_screen
s_viewport
s_canvas
s_statusbar
s_keyboard
s_control_bar
s_pinyin_ime
s_pinyin_cand_panel
s_pinyin_input_bar
s_vta
```

`s_vta` 是隐藏 textarea，用来承接 LVGL keyboard 和拼音 IME 输入，再把最终字符转发给 SSH。

### 14.2 热区

来自 `app_config.h`：

```c
APP_TERM_HOTZONE_TOP_H    100
APP_TERM_HOTZONE_LEFT_W   200
APP_TERM_HOTZONE_RIGHT_W  200
```

用途：

- 顶部热区：显示状态栏。
- 左侧热区：显示控制栏。
- 右侧热区：显示/隐藏软键盘。

状态栏高度：

```c
APP_TERM_STATUSBAR_H = 50
```

控制栏高度：

```c
APP_TERM_CONTROL_BAR_H = 50
```

### 14.3 终端输入

普通输入路径：

```text
lv_keyboard / pinyin IME
  -> hidden textarea s_vta
  -> on_vta_changed
  -> ssh_client_write
```

特殊键：

- Enter 发送 `\r`。
- Backspace 发送 `0x7F`。
- Ctrl/Alt 状态由控制栏按钮维护。
- 控制栏还能发送常见控制键或序列，具体看 `on_control_key_click()` 附近。

拼音：

- `LV_USE_IME_PINYIN` 开启时创建 `lv_ime_pinyin`。
- 词库使用 `g_app_pinyin_dict`。
- 模式是 `LV_IME_PINYIN_MODE_K26`。
- 候选栏和拼音输入栏会随键盘重新定位。

### 14.4 SSH 数据到终端状态

`on_ssh_data()` 在 SSH 接收任务上下文中调用，所以必须拿 mutex：

```c
if (xSemaphoreTake(s_term_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
    ansi_parser_feed(&s_term, data, len);
    xSemaphoreGive(s_term_mutex);
}
```

渲染定时器周期：

```c
s_render_timer = lv_timer_create(render_timer_cb, 30, NULL);
```

大约 33 FPS。渲染时读取 `s_term`，同样需要 mutex。

### 14.5 断开和错误

SSH event 回调会通过 `lv_async_call()` 或等效方式切回 UI 上下文处理，避免在 SSH 任务里直接操作 LVGL。错误会显示 msgbox，确定后关闭终端回主页。

---

## 15. ANSI/VT100 解析器

入口：`main/terminal/ansi_parser.c/.h`。

### 15.1 终端尺寸

```c
#define TERM_CELL_W       12
#define TERM_CELL_H       26
#define TERM_COLS         106
#define TERM_HISTORY_ROWS 100
#define TERM_ROWS         27
```

像素尺寸：

```text
宽度 = 106 * 12 = 1272
高度 = 27 * 26 = 702
```

这与横屏 1280x720 接近，剩余高度给状态栏/间隙/布局使用。

### 15.2 单元格

```c
typedef struct {
    uint32_t codepoint;
    uint32_t fg;
    uint32_t bg;
    uint8_t  attrs;
    uint8_t  wide;
} term_cell_t;
```

`wide`：

- `0` 普通字符。
- `1` 宽字符左半。
- `2` 宽字符右半。

属性位：

```c
TERM_ATTR_BOLD
TERM_ATTR_DIM
TERM_ATTR_ITALIC
TERM_ATTR_UNDERLINE
TERM_ATTR_BLINK
TERM_ATTR_REVERSE
TERM_ATTR_INVISIBLE
TERM_ATTR_STRIKE
```

### 15.3 状态结构

`term_state_t` 包含：

- 主屏 `cells[TERM_ROWS][TERM_COLS]`。
- 备用屏 `alt_cells[TERM_ROWS][TERM_COLS]`。
- 光标位置和可见性。
- 保存光标位置。
- 滚动区域。
- 是否备用屏。
- 历史缓冲 `history`、`history_start`、`history_count`。
- dirty 行。
- 当前前景色、背景色、属性。

历史行数是 `TERM_HISTORY_ROWS = 100`。UI 的 viewport 滚动需要通过 `ansi_parser_total_rows()` 和 `ansi_parser_get_visual_row()` 把历史与当前屏合成视觉行。

### 15.4 支持能力

解析器支持常见 TUI 必需能力：

- UTF-8 多字节解码。
- ASCII 和 Unicode codepoint 写入。
- CJK/Emoji 等宽字符识别。
- SGR 颜色和属性。
- 16 色、256 色、24-bit 真彩色。
- 光标移动：`CSI A/B/C/D/E/F/G/H/f`。
- 清屏/清行：`CSI J/K`。
- 插入/删除行：`CSI L/M`。
- 插入/删除字符：`CSI @/P`。
- 滚动：`CSI S/T`。
- 滚动区域：`CSI r`。
- 保存/恢复光标：`CSI s/u` 和 `ESC 7/8`。
- 光标显示隐藏：`?25h/l`。
- 备用屏：`?1049h/l`。
- OSC 序列忽略内容。
- 控制字符：CR、LF、BS、TAB、BEL。

限制：

- parser 上下文内部仍偏单实例设计。如果未来多终端，需要实例化 parser 上下文。
- 不保证支持所有 xterm 私有序列。

---

## 16. 终端渲染器

入口：`main/terminal/term_renderer.c/.h`。

### 16.1 Canvas

`term_renderer_create(parent)` 创建 LVGL canvas，缓冲在 PSRAM 中。尺寸由终端尺寸决定：

```text
1272 x 702 x RGB565
```

显存估算：

```text
1272 * 702 * 2 = 1,786,? bytes，约 1.7 MiB
```

再加主显示双缓冲，整个图形内存已经是数 MB 级。PSRAM 是硬需求。

### 16.2 渲染策略

- 只重绘 dirty 行。
- 每个单元格先画背景，再画字符。
- 最后画光标。
- 特殊字符优先走 `term_special_chars` 软件绘制。
- 普通字符走 LVGL label draw。

为了避免一次提交大量 draw task 撑爆 LVGL 内部内存池，渲染器按行创建/finish layer。

### 16.3 Cache 同步

ESP32-P4 + PSRAM + SMP 场景下，Canvas 缓冲写完后需要 cache 同步。渲染器里有 `esp_cache_msync(..., ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED)` 相关逻辑。不要轻易删掉。

如果出现“日志显示已刷新但屏幕不变/花屏/另一个核心读到旧像素”，优先怀疑 cache sync。

---

## 17. 特殊字符绘制

入口：`main/terminal/term_special_chars.c`。

目的：让 `btop`、`htop`、文本框线、块状图、braille 趋势图等 TUI 更像终端。

覆盖范围包括：

- Box Drawing：`U+2500 - U+257F`
- Block Elements：`U+2580 - U+259F`
- Braille：`U+2800 - U+28FF`
- Geometric Shapes：`U+25A0 - U+25FF`
- Arrows：`U+2190 - U+21FF`

这是软件近似绘制，不是完整字体。维护时重点看：

- 单线/双线 box drawing 是否和 cell 对齐。
- block element 是否正确填充比例。
- braille 点阵是否落在 cell 内。
- 箭头是否过粗或偏位。

---

## 18. 字体和中文输入

当前全局 UI 字体：

```c
#include "fonts/MI_font_20.h"
#define APP_FONT MI_font_20
#define APP_IME_CAND_FONT MI_font_20
```

用途：

- UI 中文 label。
- 终端标题。
- 拼音候选字。
- 终端渲染默认字体。

键盘使用 Montserrat 大字体，不使用中文字体，是为了英文数字符号清晰。

`sdkconfig.defaults` 开启了：

```text
CONFIG_LV_USE_IME_PINYIN=y
CONFIG_LV_IME_PINYIN_CAND_TEXT_NUM=15
# CONFIG_LV_IME_PINYIN_USE_DEFAULT_DICT is not set
```

所以项目使用自定义词库 `g_app_pinyin_dict`，不是 LVGL 默认完整词库。

---

## 19. UI 主题和风格

通用主题 token 在 `main/ui/theme.hpp`：

- 背景：`THEME_BG_PRIMARY`、`THEME_BG_SECONDARY`、`THEME_BG_PANEL`、`THEME_BG_CARD`。
- 边框：`THEME_BORDER_PRIMARY`、`THEME_BORDER_SECONDARY`。
- 文本：`THEME_TEXT_PRIMARY`、`THEME_TEXT_SECONDARY`、`THEME_TEXT_TERTIARY`。
- 状态：`THEME_SUCCESS`、`THEME_WARNING`、`THEME_DANGER`。
- 输入：`THEME_INPUT_BG`、`THEME_INPUT_BORDER`、`THEME_INPUT_FOCUS`。
- radius/spacing 常量。

注意：`ui_wifi.c` 当前仍维护了一套自己的颜色常量，不完全走 `theme.hpp`。如果要统一风格，可以先把 Wi-Fi 浮层的颜色和按钮样式迁到 `theme.hpp`，但要小步做，因为 Wi-Fi 浮层布局比较紧凑。

---

## 20. 常见开发任务怎么找

### 20.1 改 Wi-Fi 管理界面

主要看 `main/ui/ui_wifi.c`。

常用入口：

- 弹窗尺寸和左栏宽度：文件顶部 `WIFI_*` 常量。
- 状态/IP 文本：`update_state_label()`。
- 手动连接输入框：`ui_wifi_create()` 中 `row_ssid`、`row_pass`。
- 输入框光标：`apply_wifi_textarea_cursor_style()`。
- 键盘：`s_kb`、`on_ta_focus()`、`on_kb_ready()`。
- 扫描列表：`rebuild_scan_list()`。
- 保存网络列表：`rebuild_saved_networks_list()`。

### 20.2 改服务器编辑弹窗

主要看 `main/ui/ui_server_list.c`。

常用入口：

- 样式初始化：`init_styles()`。
- 表单布局重排：`relayout_form_dialog()`。
- 输入框创建：`create_textarea_field()` 附近。
- 聚焦弹键盘：`on_form_textarea_focus()`。
- 保存：`on_form_save()`。
- 加载编辑数据：`show_server_form()` 后半段。
- 图标选择：`show_icon_picker()`、`s_server_icons[]`。

### 20.3 改终端尺寸

要同步改：

- `main/terminal/ansi_parser.h` 的 `TERM_CELL_W`、`TERM_CELL_H`、`TERM_COLS`、`TERM_ROWS`。
- `term_renderer` 里依赖这些常量的 Canvas 尺寸。
- `ssh_client_connect(..., TERM_COLS, TERM_ROWS, ...)`。
- UI 里的 viewport 和 keyboard reposition 逻辑。

只改一个地方很容易导致：

- PTY 尺寸和屏幕不一致。
- TUI 换行异常。
- Canvas 裁剪。
- 光标位置错。

### 20.4 改 SSH 认证

从 `main/ssh/ssh_client.c` 入手：

- 密码认证位置：`ssh_userauth_password()`。
- known_hosts 行为：`verify_knownhost()`。
- PTY 类型：`ssh_channel_request_pty_size(..., "xterm-256color", ...)`。

加公钥认证时建议：

- 先决定私钥存储位置，优先 SPIFFS。
- UI 表单增加认证类型。
- `server_entry_t` 需要版本迁移。
- 不要直接在现有 struct 中间插字段，尽量尾部追加并处理旧 blob。

### 20.5 改 NVS 数据结构

涉及：

- `server_entry_t`
- `wifi_store_t`
- NVS key 和 blob size

规则：

- 字段长度是存储协议的一部分。
- 改结构体大小要保留 legacy 读取逻辑。
- 删除字段比追加字段风险更大。
- 服务器图标 id 数组不要重排。

### 20.6 改字体

看：

- `main/app_config.h`
- `main/fonts/`
- `main/CMakeLists.txt` 的 `file(GLOB app_font_sources "fonts/*.c")`
- `target_compile_options(... -include app_config.h)`

如果新增字体：

1. 放入 `main/fonts/`。
2. 头文件声明 `extern const lv_font_t xxx;`。
3. `app_config.h` include 新字体头。
4. 修改 `APP_FONT` / `APP_IME_CAND_FONT`。
5. 注意 flash 和 build 时间。

---

## 21. 线程和并发边界

关键事实：

- LVGL 不是线程安全的。
- SSH 接收任务不在 LVGL 线程。
- `term_state_t` 同时被 SSH 接收和 LVGL 渲染访问。

终端状态保护：

```c
static SemaphoreHandle_t s_term_mutex;
```

写入路径：

```text
ssh_recv_task
  -> s_data_cb
  -> ui_terminal:on_ssh_data
  -> xSemaphoreTake
  -> ansi_parser_feed
  -> xSemaphoreGive
```

读取/渲染路径：

```text
LVGL render_timer_cb
  -> xSemaphoreTake
  -> term_renderer_refresh
  -> xSemaphoreGive
```

UI 操作原则：

- 从非 LVGL 任务处理 UI，需要用 `lv_async_call()` 或回到 LVGL 上下文。
- 创建页面时持有 `bsp_display_lock()`。
- timer 回调一般已经在 LVGL 上下文，但仍要小心共享状态。

---

## 22. 内存和性能

主要内存大户：

1. LVGL 显示双缓冲。
2. 终端 Canvas。
3. 中文字体。
4. libssh 连接和接收缓冲。
5. LVGL 对象树和键盘/拼音面板。

显示缓冲估算：

```text
720 * 1280 * 2 bytes = 1.76 MiB
双缓冲约 3.52 MiB
```

终端 Canvas：

```text
1272 * 702 * 2 bytes 约 1.7 MiB
```

所以图形相关就已经约 5 MiB。后续新增大图、离屏 canvas、动画缓冲时要非常谨慎。

性能策略：

- 终端只重绘 dirty 行。
- 渲染周期 30 ms。
- 特殊字符软件绘制尽量简单。
- Canvas 缓冲在 PSRAM，但需要 cache 同步。
- 编译使用性能优化：`CONFIG_COMPILER_OPTIMIZATION_PERF=y`。

---

## 23. 当前限制和已知风险

功能限制：

- 只支持单 SSH 会话。
- 只支持单终端实例。
- SSH 只支持密码认证。
- known_hosts 首次未知主机自动接受，没有用户确认 UI。
- 服务器和 Wi-Fi 密码明文 NVS。
- 终端不是完整 xterm，只覆盖常用 TUI 序列。
- parser 内部仍有单实例假设，多终端需要重构。

UI 限制：

- Wi-Fi 浮层和主页主题还没有完全统一。
- 终端热区是固定像素参数，换屏幕方向/尺寸要重调。
- 软键盘遮挡处理依赖估算高度。

存储限制：

- 服务器最多 10 个。
- Wi-Fi 保存最多 8 个。
- 扫描结果最多 16 个。
- 字段长度固定，超长会截断。

安全风险：

- 密码明文。
- 首次主机自动信任。
- 没有主机指纹确认。
- 没有配置加密。

---

## 24. 调试建议

启动阶段看日志：

- `main`：启动、显示初始化、Wi-Fi 自动连接。
- `wifi_mgr`：C6 上电、esp_wifi 初始化、扫描、连接、IP 事件。
- `ui_srv_list`：主页刷新、服务器操作。
- `ui_wifi`：扫描列表、连接请求。
- `ui_term`：终端打开、SSH 数据量、渲染、热区。
- `ssh_client`：连接、认证、PTY、auto_cmd、接收任务。

常见问题：

### 屏幕不亮

检查：

- `bsp_display_start_with_config()` 是否返回。
- `lv_display_set_rotation()` 后是否有对象。
- `bsp_display_brightness_set(45)` 是否成功。
- PSRAM 是否启用。

### Wi-Fi 不可用

检查：

- `bsp_feature_enable(BSP_FEATURE_WIFI, true)`。
- ESP Hosted SDIO GPIO 是否和板子一致。
- `esp_wifi_init()` 返回值。
- `s_wifi_unavailable` 是否被置 true。

### Wi-Fi 扫描不到

检查：

- Wi-Fi 是否已经 start。
- 扫描时是否正在连接。
- `WIFI_MAX_SCAN_RESULTS` 是否太小。
- AP 是否隐藏 SSID。

### SSH 连不上

检查：

- Wi-Fi 是否有 IPv4。
- 服务器 host/port/user/password。
- `ssh_connect()` 错误 reason。
- known_hosts 是否 changed。
- 服务器是否允许 password auth。

### 终端有输出但屏幕不刷新

检查：

- `on_ssh_data()` 是否拿到数据。
- `ansi_parser_feed()` 是否置 dirty。
- `render_timer_cb` 是否运行。
- `s_term_mutex` 是否超时。
- `term_renderer_refresh()` 是否被调用。
- cache sync 是否被保留。

### TUI 显示错位

检查：

- `TERM_COLS/TERM_ROWS` 是否和 PTY 请求一致。
- 字体实际 cell 宽高是否仍是 `12 x 26`。
- 宽字符判断。
- 特殊字符绘制。
- 服务器端 `$TERM` 是否适配 `xterm-256color`。

---

## 25. 建议的后续改进顺序

优先级较高：

1. known_hosts 首次连接确认 UI。
2. 密码/凭据安全存储方案。
3. 公钥认证和私钥 SPIFFS 存储。
4. SSH/parser/renderer 实例化，为多会话铺路。
5. 终端 Ctrl/Alt/方向键/功能键工具栏增强。
6. Wi-Fi 浮层主题统一和布局响应式整理。
7. 配置导入导出。
8. NVS schema version 明确化。
9. 硬件键盘/USB HID 输入接入。

---

## 26. 给后续 AI 的接手提示

如果你是另一个 AI，建议按这个顺序读代码：

1. `main/main.c`：知道启动顺序。
2. `main/app_config.h`：知道字体和热区参数。
3. `main/ui/ui_server_list.c`：知道主页如何组织。
4. `main/ui/ui_wifi.c`：知道 Wi-Fi 浮层如何操作 manager。
5. `main/wifi_manager.c`：知道 Wi-Fi 状态机和 NVS。
6. `main/ui/ui_terminal.c`：知道 UI、SSH、parser、renderer 怎么接起来。
7. `main/ssh/ssh_client.c`：知道连接和接收任务。
8. `main/terminal/ansi_parser.c`：知道终端状态怎么变化。
9. `main/terminal/term_renderer.c`：知道 Canvas 怎么画。
10. `main/storage/server_config.c`：知道服务器配置怎么落盘。

改代码时优先保持现有边界：

- UI 不要直接操作 NVS，走 manager/config 模块。
- SSH 不要直接操作 LVGL。
- parser 不要画图。
- renderer 不要解析 SSH。
- 非 LVGL 任务不要直接碰 UI。
- 修改持久化结构时先考虑迁移。

---

## 27. 当前工程总结

这是一个运行在 `ESP32-P4 + ESP32-C6` 双芯片架构上的触摸式 SSH 终端。P4 负责 UI、终端渲染和 SSH 客户端逻辑；C6 通过 SDIO/ESP Hosted 提供 Wi-Fi。项目使用 LVGL 构建触摸界面，用 libssh 打开 PTY shell，用自研 ANSI/VT100 解析器维护终端状态，再用 LVGL Canvas 渲染成可交互终端画面。

维护这个项目时，要始终记住四条主线：

1. 网络由 `wifi_manager` 管，UI 只是展示和发起动作。
2. SSH 由 `ssh_client` 管，终端页只接收事件和字节流。
3. 字节流由 `ansi_parser` 变成 `term_state_t`。
4. `term_renderer` 只把 `term_state_t` 画出来。

只要这四条边界不乱，后续扩展会比较稳。
