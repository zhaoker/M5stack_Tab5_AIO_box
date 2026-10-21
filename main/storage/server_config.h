/**
 * @file server_config.h
 * @brief 服务器配置 NVS 存储接口
 *
 * 最多支持 MAX_SERVERS 个服务器条目，持久化到 NVS。
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#define MAX_SERVERS          10
#define SERVER_NVS_NAMESPACE "srv_cfg"

/** 服务器连接条目 */
typedef struct {
    char     name[32];       /**< 显示名称 */
    char     host[64];       /**< 主机名或 IP */
    uint16_t port;           /**< SSH 端口（默认 22） */
    char     username[32];   /**< 登录用户名 */
    char     password[64];   /**< 登录密码 */
    char     auto_cmd[128];  /**< 连接后自动执行的命令（如 "btop"） */
    uint8_t  icon_id;        /**< UI icon index */
} server_entry_t;

/**
 * @brief 初始化服务器配置模块（打开 NVS，读取数量）
 */
esp_err_t server_config_init(void);

/**
 * @brief 获取已保存的服务器数量
 * @return 0 ~ MAX_SERVERS
 */
int server_config_count(void);

/**
 * @brief 加载指定索引的服务器条目
 * @param index  0-based 索引
 * @param out    输出缓冲区
 */
esp_err_t server_config_load(int index, server_entry_t *out);

/**
 * @brief 加载不含密码的服务器公开信息；密码解密失败时也可用于 UI 展示和重新编辑。
 */
esp_err_t server_config_load_public(int index, server_entry_t *out);

/**
 * @brief 保存/覆盖指定索引的服务器条目
 * @param index  0-based 索引
 * @param entry  要保存的数据
 */
esp_err_t server_config_save(int index, const server_entry_t *entry);

/**
 * @brief 追加一个新服务器条目（自动递增数量）
 */
esp_err_t server_config_append(const server_entry_t *entry);

/**
 * @brief 清空所有服务器条目
 */
esp_err_t server_config_clear_all(void);

/**
 * @brief 删除指定索引的服务器条目（后续条目前移）
 */
esp_err_t server_config_delete(int index);

#ifdef __cplusplus
}
#endif
