/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief ESP BSP: ESP32-P4 Function EV Board
 */

#pragma once

#include "sdkconfig.h"
#include "bsp/config.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/sdmmc_host.h"
#if (BSP_CONFIG_NO_AUDIO == 0)
#include "driver/i2s_std.h"
#include "driver/i2s_tdm.h"
#include "esp_codec_dev.h"
#endif  // BSP_CONFIG_NO_AUDIO == 0
#include "bsp/display.h"
#include "sdkconfig.h"

/**************************************************************************************************
 *  BSP Capabilities
 **************************************************************************************************/
#define BSP_CAPS_DISPLAY       1
#define BSP_CAPS_TOUCH         1
#define BSP_CAPS_BUTTONS       0
#if (BSP_CONFIG_NO_AUDIO == 0)
#define BSP_CAPS_AUDIO         1
#define BSP_CAPS_AUDIO_SPEAKER 1
#define BSP_CAPS_AUDIO_MIC     1
#else
#define BSP_CAPS_AUDIO         0
#define BSP_CAPS_AUDIO_SPEAKER 0
#define BSP_CAPS_AUDIO_MIC     0
#endif
#define BSP_CAPS_SDCARD        1
#define BSP_CAPS_IMU           0

/**************************************************************************************************
 *  ESP-BOX pinout
 **************************************************************************************************/
/* SYS I2C */
#define BSP_I2C_NUM 0
#define BSP_I2C_SCL (GPIO_NUM_32)
#define BSP_I2C_SDA (GPIO_NUM_31)

/* EXT I2C */
#define BSP_EXT_I2C_NUM 1
#define BSP_EXT_I2C_SCL (GPIO_NUM_54)
#define BSP_EXT_I2C_SDA (GPIO_NUM_53)

// /* Ext Keyboard */
// #define TAB5_TCA8418_INT_PIN 50 // Interrupt input

/* Audio */
#define BSP_I2S_SCLK     (GPIO_NUM_27)  // Bit clock       BSP_I2S_BCLK  <--> ES7210/ESP311 I2S_BCLK
#define BSP_I2S_MCLK     (GPIO_NUM_30)  // Master clock    BSP_I2S_MCLK  <--> ES7210/ESP311 I2S_MCLK
#define BSP_I2S_LCLK     (GPIO_NUM_29)  // Word select     BSP_I2S_WR    <--> ES7210/ESP311 I2S_WR
#define BSP_I2S_DOUT     (GPIO_NUM_26)  // Data output     BSP_I2S_DOUT  ---> ES8388        I2S_DSIN
#define BSP_I2S_DSIN     (GPIO_NUM_28)  // Data input      BSP_I2S_DIN   <--- ES7210        I2S_DOUT
#define BSP_POWER_AMP_IO (GPIO_NUM_NC)  // (GPIO_NUM_53)

/* Display */
#define BSP_LCD_BACKLIGHT (GPIO_NUM_22)
#define BSP_LCD_RST       (GPIO_NUM_NC)  //
#define BSP_LCD_TOUCH_RST (GPIO_NUM_NC)  // Controlled by IO expander
#define BSP_LCD_TOUCH_INT (GPIO_NUM_NC)  // 23

/* uSD card */
#define BSP_SD_D0  (GPIO_NUM_39)
#define BSP_SD_D1  (GPIO_NUM_40)
#define BSP_SD_D2  (GPIO_NUM_41)
#define BSP_SD_D3  (GPIO_NUM_42)
#define BSP_SD_CMD (GPIO_NUM_44)
#define BSP_SD_CLK (GPIO_NUM_43)

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t bsp_cam_osc_init(void);

/**************************************************************************************************
 *
 * I2C interface
 *
 * There are multiple devices connected to I2C peripheral:
 *  - Codec ES8311 (configuration only)
 *  - LCD Touch controller
 **************************************************************************************************/

/**
 * @brief Init I2C driver
 *
 * @return
 *      - ESP_OK                On success
 *      - ESP_ERR_INVALID_ARG   I2C parameter error
 *      - ESP_FAIL              I2C driver installation error
 *
 */
esp_err_t bsp_i2c_init(void);

/**
 * @brief Deinit I2C driver and free its resources
 *
 * @return
 *      - ESP_OK                On success
 *      - ESP_ERR_INVALID_ARG   I2C parameter error
 *
 */
esp_err_t bsp_i2c_deinit(void);

/**
 * @brief Get I2C driver handle
 *
 * @return
 *      - I2C handle
 *
 */
i2c_master_bus_handle_t bsp_i2c_get_handle(void);

esp_err_t bsp_i2c_scan();

esp_err_t bsp_ext_i2c_init(void);
esp_err_t bsp_ext_i2c_deinit(void);
i2c_master_bus_handle_t bsp_ext_i2c_get_handle(void);

esp_err_t bsp_grove_i2c_init(void);
esp_err_t bsp_grove_i2c_deinit(void);
i2c_master_bus_handle_t bsp_grove_i2c_get_handle(void);

/**************************************************************************************************
 *
 * I2S audio interface
 *
 * There are two devices connected to the I2S peripheral:
 *  - Codec ES8311 for output(playback) and input(recording) path
 *
 * For speaker initialization use bsp_audio_codec_speaker_init() which is inside initialize I2S with bsp_audio_init().
 * For microphone initialization use bsp_audio_codec_microphone_init() which is inside initialize I2S with
 *bsp_audio_init(). After speaker or microphone initialization, use functions from esp_codec_dev for play/record audio.
 * Example audio play:
 * \code{.c}
 * esp_codec_dev_set_out_vol(spk_codec_dev, DEFAULT_VOLUME);
 * esp_codec_dev_open(spk_codec_dev, &fs);
 * esp_codec_dev_write(spk_codec_dev, wav_bytes, bytes_read_from_spiffs);
 * esp_codec_dev_close(spk_codec_dev);
 * \endcode
 **************************************************************************************************/
#if (BSP_CONFIG_NO_AUDIO == 0)

/**
 * @brief Init audio
 *
 * @note There is no deinit audio function. Users can free audio resources by calling i2s_del_channel()
 * @warning The type of i2s_config param is depending on IDF version.
 * @param[in]  i2s_config I2S configuration. Pass NULL to use default values (Mono, duplex, 16bit, 22050 Hz)
 * @return
 *      - ESP_OK                On success
 *      - ESP_ERR_NOT_SUPPORTED The communication mode is not supported on the current chip
 *      - ESP_ERR_INVALID_ARG   NULL pointer or invalid configuration
 *      - ESP_ERR_NOT_FOUND     No available I2S channel found
 *      - ESP_ERR_NO_MEM        No memory for storing the channel information
 *      - ESP_ERR_INVALID_STATE This channel has not initialized or already started
 */
esp_err_t bsp_audio_init(const i2s_std_config_t *i2s_config);

/**
 * @brief Initialize speaker codec device
 *
 * @return Pointer to codec device handle or NULL when error occurred
 */
esp_codec_dev_handle_t bsp_audio_codec_speaker_init(void);

/**
 * @brief Initialize microphone codec device
 *
 * @return Pointer to codec device handle or NULL when error occurred
 */
esp_codec_dev_handle_t bsp_audio_codec_microphone_init(void);

typedef esp_err_t (*bsp_i2s_read_fn)(void *audio_buffer, size_t len, size_t *bytes_read, uint32_t timeout_ms);
typedef esp_err_t (*bsp_i2s_write_fn)(void *audio_buffer, size_t len, size_t *bytes_written, uint32_t timeout_ms);
typedef esp_err_t (*bsp_codec_set_in_gain_fn)(float gain);
typedef esp_err_t (*bsp_codec_mute_fn)(bool enable);
typedef int (*bsp_codec_volume_fn)(int volume);
typedef esp_err_t (*bsp_codec_get_volume_fn)(void);
typedef esp_err_t (*bsp_codec_reconfig_fn)(uint32_t rate, uint32_t bps, i2s_slot_mode_t ch);
typedef esp_err_t (*bsp_i2s_reconfig_clk_fn)(uint32_t rate, uint32_t bits_cfg, i2s_slot_mode_t ch);

typedef struct {
    bsp_i2s_read_fn i2s_read;
    bsp_i2s_write_fn i2s_write;
    bsp_codec_mute_fn set_mute;
    bsp_codec_volume_fn set_volume;
    bsp_codec_get_volume_fn get_volume;
    bsp_codec_set_in_gain_fn set_in_gain;
    bsp_codec_reconfig_fn codec_reconfig_fn;
    bsp_i2s_reconfig_clk_fn i2s_reconfig_clk_fn;
} bsp_codec_config_t;

void bsp_codec_init(void);
bsp_codec_config_t *bsp_get_codec_handle(void);
uint8_t bsp_codec_feed_channel(void);
#endif  // BSP_CONFIG_NO_AUDIO == 0

/**************************************************************************************************
 *
 * SPIFFS
 *
 * After mounting the SPIFFS, it can be accessed with stdio functions ie.:
 * \code{.c}
 * FILE* f = fopen(BSP_SPIFFS_MOUNT_POINT"/hello.txt", "w");
 * fprintf(f, "Hello World!\n");
 * fclose(f);
 * \endcode
 **************************************************************************************************/
#define BSP_SPIFFS_MOUNT_POINT CONFIG_BSP_SPIFFS_MOUNT_POINT

/**
 * @brief Mount SPIFFS to virtual file system
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_STATE if esp_vfs_spiffs_register was already called
 *      - ESP_ERR_NO_MEM if memory can not be allocated
 *      - ESP_FAIL if partition can not be mounted
 *      - other error codes
 */
esp_err_t bsp_spiffs_mount(void);

/**
 * @brief Unmount SPIFFS from virtual file system
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_NOT_FOUND if the partition table does not contain SPIFFS partition with given label
 *      - ESP_ERR_INVALID_STATE if esp_vfs_spiffs_unregister was already called
 *      - ESP_ERR_NO_MEM if memory can not be allocated
 *      - ESP_FAIL if partition can not be mounted
 *      - other error codes
 */
esp_err_t bsp_spiffs_unmount(void);

/**************************************************************************************************
 *
 * uSD card
 *
 * After mounting the uSD card, it can be accessed with stdio functions ie.:
 * \code{.c}
 * FILE* f = fopen(BSP_MOUNT_POINT"/hello.txt", "w");
 * fprintf(f, "Hello %s!\n", bsp_sdcard->cid.name);
 * fclose(f);
 * \endcode
 **************************************************************************************************/
/**
 * @brief Init SD crad
 *
 * @param mount_point Path where partition should be registered (e.g. "/sdcard")
 * @param max_files Maximum number of files which can be open at the same time
 * @return
 *    - ESP_OK                  Success
 *    - ESP_ERR_INVALID_STATE   If esp_vfs_fat_register was already called
 *    - ESP_ERR_NOT_SUPPORTED   If dev board not has SDMMC/SDSPI
 *    - ESP_ERR_NO_MEM          If not enough memory or too many VFSes already registered
 *    - Others                  Fail
 */
esp_err_t bsp_sdcard_init(char *mount_point, size_t max_files);

/**
 * @brief Deinit SD card
 *
 * @param mount_point Path where partition was registered (e.g. "/sdcard")
 * @return
 *    - ESP_OK: Success
 *    - Others: Fail
 */
esp_err_t bsp_sdcard_deinit(char *mount_point);

/**************************************************************************************************
 *
 * LCD interface
 *
 **************************************************************************************************/
#define BSP_LCD_PIXEL_CLOCK_MHZ (80)

/**
 * @brief Get display panel IC name
 *
 * @return String with display panel IC name (e.g., "ILI9881C", "ST7123")
 */
const char *bsp_display_get_panel_ic(void);

void bsp_io_expander_pi4ioe_init(i2c_master_bus_handle_t bus_handle);

void bsp_set_charge_qc_en(bool en);

void bsp_set_charge_en(bool en);

void bsp_set_usb_5v_en(bool en);

void bsp_set_ext_5v_en(bool en);

void bsp_generate_poweroff_signal();

bool bsp_headphone_detect();

void bsp_set_ext_antenna_enable(bool en);

void bsp_set_wifi_power_enable(bool en);

void bsp_reset_tp();

bool bsp_usb_c_detect();

bool bsp_usb_a_detect();

#ifdef __cplusplus
}
#endif
