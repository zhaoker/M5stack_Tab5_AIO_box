/**
 * @file ui_terminal.h
 * @brief SSH 终端界面接口
 *
 * 建立 SSH 连接后显示全屏终端画面：
 *   - lv_canvas 渲染 ANSI/VT100 内容
 *   - 底部弹出式软键盘（触摸调出）
 *   - 顶部悬浮工具栏（断开连接、调整字体等）
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include "storage/server_config.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t id;
    char name[64];
    uint8_t icon_id;
    bool connected;
    bool failed;
} ui_terminal_session_info_t;

/**
 * @brief 打开终端界面并建立 SSH 连接
 * @param server  服务器配置（传入拷贝，函数内部保存）
 */
void ui_terminal_open(const server_entry_t *server);

void ui_terminal_resume_session(uint32_t session_id);

int ui_terminal_session_count(void);

bool ui_terminal_has_active_ssh_session(void);

bool ui_terminal_session_get_info(int index, ui_terminal_session_info_t *out);

/**
 * @brief 关闭终端界面，断开 SSH 连接，返回服务器列表
 */
void ui_terminal_close(void);

#ifdef __cplusplus
}
#endif
