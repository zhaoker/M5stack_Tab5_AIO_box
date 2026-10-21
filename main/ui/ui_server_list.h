/**
 * @file ui_server_list.h
 * @brief 服务器列表主界面接口
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

/**
 * @brief 创建服务器列表界面
 * @param parent 父容器，通常为 lv_scr_act()
 */
void ui_server_list_create(lv_obj_t *parent);

/**
 * @brief 刷新服务器列表和 WiFi 状态
 */
void ui_server_list_refresh(void);

void ui_server_list_show_power_detail(void);
void ui_server_list_show_wifi_panel(void);

#ifdef __cplusplus
}
#endif
