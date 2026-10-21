/**
 * @file wifi_manager.h
 * @brief WiFi 连接管理（ESP32-P4 通过 SDIO 驱动 ESP32-C6 模块）
 *
 * 流程：
 *   1. bsp_feature_enable(BSP_FEATURE_WIFI, true) 给 C6 上电（由 wifi_manager_init 调用）
 *   2. 调用标准 esp_wifi API 扫描 / 连接
 *   3. 凭据持久化于 NVS namespace "wifi_creds"
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#define WIFI_NVS_NAMESPACE "wifi_creds"
#define WIFI_MAX_SSID_LEN  64
#define WIFI_MAX_PASS_LEN  64
#define WIFI_MAX_SAVED_NETWORKS 8
#define WIFI_MAX_SCAN_RESULTS   16
#define WIFI_MAX_IPV6_ADDRS     3
#define WIFI_IPV6_TEXT_LEN      48

/** WiFi 连接状态 */
typedef enum {
    WIFI_STATE_IDLE,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_DISCONNECTED,
    WIFI_STATE_FAILED,
} wifi_state_t;

/** WiFi 状态变化回调 */
typedef void (*wifi_state_cb_t)(wifi_state_t state, void *user_data);

typedef struct {
    char ssid[WIFI_MAX_SSID_LEN];
    char password[WIFI_MAX_PASS_LEN];
} wifi_saved_network_t;

typedef struct {
    char ssid[WIFI_MAX_SSID_LEN];
    int8_t rssi;
    uint8_t authmode;
    bool saved;
    bool connected;
} wifi_scan_result_t;

/**
 * @brief 初始化 WiFi 管理器（上电 C6、初始化 TCP/IP 与 WiFi 栈）
 */
esp_err_t wifi_manager_init(void);

/**
 * @brief 注册状态变化回调
 */
void wifi_manager_set_cb(wifi_state_cb_t cb, void *user_data);

/**
 * @brief 使用存储的凭据自动连接（若 NVS 中有保存的 SSID/密码）
 * @return ESP_OK 表示凭据存在并已发起连接，ESP_ERR_NOT_FOUND 表示无凭据
 */
esp_err_t wifi_manager_auto_connect(void);

/**
 * @brief 连接到指定 AP，并可选择保存凭据到 NVS
 */
esp_err_t wifi_manager_connect(const char *ssid, const char *password, bool save_creds);

/**
 * @brief 断开当前连接
 */
esp_err_t wifi_manager_disconnect(void);

/**
 * @brief 获取当前连接状态
 */
wifi_state_t wifi_manager_get_state(void);

bool wifi_manager_has_ipv4(void);
bool wifi_manager_has_ipv6_global(void);
esp_err_t wifi_manager_get_current_ip_text(char *ipv4_buf, size_t ipv4_len,
                                           char *ipv6_buf, size_t ipv6_len);
esp_err_t wifi_manager_get_all_ipv6_text(char addrs[][WIFI_IPV6_TEXT_LEN],
                                         size_t max_items,
                                         size_t *out_count);

/**
 * @brief 获取已保存的 SSID（若无则返回 ESP_ERR_NOT_FOUND）
 */
esp_err_t wifi_manager_get_saved_ssid(char *ssid_buf, size_t buf_len);

/**
 * @brief 获取当前已保存的 WiFi 列表
 * @return 实际写入 out_networks 的条目数
 */
size_t wifi_manager_get_saved_networks(wifi_saved_network_t *out_networks, size_t max_items);

/**
 * @brief 保存或更新一个 WiFi 网络凭据
 */
esp_err_t wifi_manager_save_network(const char *ssid, const char *password);

/**
 * @brief 清空所有已保存的 WiFi 网络
 */
esp_err_t wifi_manager_clear_saved_networks(void);

/**
 * @brief 删除一个已保存的 WiFi 网络
 */
esp_err_t wifi_manager_delete_network(const char *ssid);

/**
 * @brief 使用已保存凭据连接指定 WiFi
 */
esp_err_t wifi_manager_connect_saved(const char *ssid);

/**
 * @brief 扫描当前可用 WiFi 列表
 * @return ESP_OK 表示扫描成功
 */
esp_err_t wifi_manager_scan_networks(wifi_scan_result_t *out_results, size_t max_items, size_t *out_count);

/**
 * @brief 获取当前已连接的 SSID
 */
esp_err_t wifi_manager_get_current_ssid(char *ssid_buf, size_t buf_len);

/**
 * @brief 判断指定 SSID 是否已保存
 */
bool wifi_manager_is_saved_network(const char *ssid);

#ifdef __cplusplus
}
#endif
