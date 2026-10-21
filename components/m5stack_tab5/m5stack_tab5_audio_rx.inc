static bool i2s_rx_tdm_ready = false;

static esp_err_t bsp_audio_init_rx_tdm(void)
{
    if (i2s_rx_tdm_ready) {
        return ESP_OK;
    }
    if (i2s_rx_chan == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    i2s_tdm_config_t tdm_cfg = {
        .clk_cfg =
            {
                .sample_rate_hz  = (uint32_t)48000,
                .clk_src         = I2S_CLK_SRC_DEFAULT,
                .ext_clk_freq_hz = 0,
                .mclk_multiple   = I2S_MCLK_MULTIPLE_256,
                .bclk_div        = 8,
            },
        .slot_cfg = {.data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
                     .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
                     .slot_mode      = I2S_SLOT_MODE_STEREO,
                     .slot_mask      = (I2S_TDM_SLOT0 | I2S_TDM_SLOT1 | I2S_TDM_SLOT2 | I2S_TDM_SLOT3),
                     .ws_width       = I2S_TDM_AUTO_WS_WIDTH,
                     .ws_pol         = false,
                     .bit_shift      = true,
                     .left_align     = false,
                     .big_endian     = false,
                     .bit_order_lsb  = false,
                     .skip_mask      = false,
                     .total_slot     = I2S_TDM_AUTO_SLOT_NUM},
        .gpio_cfg = BSP_I2S_GPIO_CFG,
    };

    ESP_RETURN_ON_ERROR(i2s_channel_init_tdm_mode(i2s_rx_chan, &tdm_cfg), TAG, "init I2S RX TDM failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(i2s_rx_chan), TAG, "enable I2S RX failed");
    i2s_rx_tdm_ready = true;
    return ESP_OK;
}
