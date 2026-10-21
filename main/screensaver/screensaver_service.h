#pragma once

#include "esp_err.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 屏保默认布局手工调参区。
 * 坐标含义：X/Y 为元素左上角距离屏幕左上角的像素位置，屏幕范围 1280x720。
 * 字号含义：EXIF标题固定使用 MI_font_20；时间/秒钟/日期/农历可选 20/40/60/80/120/160/200。
 * 颜色使用 0xRRGGBB。显示开关 true 为显示，false 为隐藏。
 * 文字阴影只作用于时间/秒钟/日期/农历，长度为阴影向右下方偏移的像素。
 * 修改这些默认值后，“复位屏保元素布局”会恢复到这里定义的坐标、字号、显示状态、颜色和文字阴影。
 */
#define SCREENSAVER_LAYOUT_SCREEN_W 1280
#define SCREENSAVER_LAYOUT_SCREEN_H 720

#define SCREENSAVER_DEFAULT_TEXT_SHADOW_ENABLED true
#define SCREENSAVER_DEFAULT_TEXT_SHADOW_LENGTH 2

#define SCREENSAVER_DEFAULT_TITLE_X 38
#define SCREENSAVER_DEFAULT_TITLE_Y 28
#define SCREENSAVER_DEFAULT_TITLE_FONT_SIZE 20  //固定不得修改
#define SCREENSAVER_DEFAULT_TITLE_VISIBLE true
#define SCREENSAVER_DEFAULT_TITLE_COLOR 0xFFFFFF

#define SCREENSAVER_DEFAULT_TIME_X 560
#define SCREENSAVER_DEFAULT_TIME_Y 330
#define SCREENSAVER_DEFAULT_TIME_FONT_SIZE 200
#define SCREENSAVER_DEFAULT_TIME_VISIBLE true
#define SCREENSAVER_DEFAULT_TIME_COLOR 0xFFFFFF

#define SCREENSAVER_DEFAULT_SECOND_X 1100
#define SCREENSAVER_DEFAULT_SECOND_Y 400
#define SCREENSAVER_DEFAULT_SECOND_FONT_SIZE 120
#define SCREENSAVER_DEFAULT_SECOND_VISIBLE true
#define SCREENSAVER_DEFAULT_SECOND_COLOR 0xFFFFFF

#define SCREENSAVER_DEFAULT_DATE_X 510
#define SCREENSAVER_DEFAULT_DATE_Y 540
#define SCREENSAVER_DEFAULT_DATE_FONT_SIZE 60
#define SCREENSAVER_DEFAULT_DATE_VISIBLE true
#define SCREENSAVER_DEFAULT_DATE_COLOR 0xFFFFFF

#define SCREENSAVER_DEFAULT_LUNAR_X 700
#define SCREENSAVER_DEFAULT_LUNAR_Y 630
#define SCREENSAVER_DEFAULT_LUNAR_FONT_SIZE 60
#define SCREENSAVER_DEFAULT_LUNAR_VISIBLE true
#define SCREENSAVER_DEFAULT_LUNAR_COLOR 0xFFFFFF

esp_err_t screensaver_service_init(void);
void screensaver_service_notify_activity(void);
void screensaver_service_enter_now(void);
bool screensaver_service_is_active(void);
void screensaver_service_reload_settings(void);

#ifdef __cplusplus
}
#endif
