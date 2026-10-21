/**
 * @file ssh_client.h
 * @brief SSH 客户端接口（基于 LibSSH-ESP32）
 *
 * 打开一个带 PTY 的 SSH Shell 会话，将接收到的数据通过回调传给 ANSI 解析器。
 * 发送与接收由各自独立的 FreeRTOS 任务处理。
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"
#include "storage/server_config.h"

/** SSH 数据接收回调（从 SSH 接收任务上下文调用） */
typedef void (*ssh_data_cb_t)(const char *data, size_t len, void *user_data);

/** SSH 连接状态回调 */
typedef enum {
    SSH_EVT_CONNECTED,
    SSH_EVT_DISCONNECTED,
    SSH_EVT_AUTH_FAILED,
    SSH_EVT_ERROR,
} ssh_event_t;
typedef void (*ssh_event_cb_t)(ssh_event_t event, const char *reason, void *user_data);

typedef struct ssh_client ssh_client_t;

ssh_client_t *ssh_client_create(void);
void ssh_client_destroy(ssh_client_t *client);

esp_err_t ssh_client_connect_handle(ssh_client_t *client,
                                    const server_entry_t *cfg,
                                    int cols, int rows,
                                    ssh_data_cb_t data_cb,
                                    ssh_event_cb_t event_cb,
                                    void *user_data);
int ssh_client_write_handle(ssh_client_t *client, const char *data, size_t len);
void ssh_client_resize_handle(ssh_client_t *client, int cols, int rows);
void ssh_client_disconnect_handle(ssh_client_t *client);
bool ssh_client_is_connected_handle(const ssh_client_t *client);

/**
 * @brief 建立 SSH 连接并打开 PTY Shell
 *
 * 调用后在后台任务中持续读取数据，通过 data_cb 回调上报。
 *
 * @param cfg       服务器配置（host/port/user/pass/auto_cmd）
 * @param cols      终端列数
 * @param rows      终端行数
 * @param data_cb   数据接收回调
 * @param event_cb  连接事件回调（可为 NULL）
 * @param user_data 用户数据指针（会传给两个回调）
 */
esp_err_t ssh_client_connect(const server_entry_t *cfg,
                             int cols, int rows,
                             ssh_data_cb_t data_cb,
                             ssh_event_cb_t event_cb,
                             void *user_data);

/**
 * @brief 向 SSH 通道写入数据（键盘输入 / 控制序列）
 */
int ssh_client_write(const char *data, size_t len);

/**
 * @brief 通知服务端 PTY 尺寸变更
 */
void ssh_client_resize(int cols, int rows);

/**
 * @brief 断开当前 SSH 连接并释放资源
 */
void ssh_client_disconnect(void);

/**
 * @brief 查询当前连接状态
 */
bool ssh_client_is_connected(void);

#ifdef __cplusplus
}
#endif
