/**
 * @file ui_wifi.h
 * @brief WiFi 配置界面接口
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

/**
 * @brief 在 parent 上创建 WiFi 配置浮层
 * @param parent  通常为 lv_scr_act()
 */
void ui_wifi_create(lv_obj_t *parent);

#ifdef __cplusplus
}
#endif
