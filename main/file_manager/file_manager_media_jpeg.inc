static uint8_t *fm_media_read_file(const char *path, size_t *out_size)
{
    FILE *fp = fopen(path, "rb");
    long file_size;
    uint8_t *buffer;

    if (!fp) {
        return NULL;
    }
    fseek(fp, 0, SEEK_END);
    file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (file_size <= 0 || (uint32_t)file_size > FM_MEDIA_MAX_HW_JPEG_BYTES) {
        fclose(fp);
        return NULL;
    }

    /* 读取 JPEG 文件本身不需要内部 DMA，先放到 PSRAM，避免刚开机 WiFi/SDIO 占用 DMA 时误判为不能看图。 */
    buffer = (uint8_t *)heap_caps_malloc((size_t)file_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buffer) {
        buffer = (uint8_t *)heap_caps_malloc((size_t)file_size, MALLOC_CAP_8BIT);
    }
    if (!buffer) {
        buffer = (uint8_t *)malloc((size_t)file_size);
    }
    if (!buffer) {
        fclose(fp);
        return NULL;
    }
    if (fread(buffer, 1, (size_t)file_size, fp) != (size_t)file_size) {
        free(buffer);
        fclose(fp);
        return NULL;
    }
    fclose(fp);

    if (out_size) {
        *out_size = (size_t)file_size;
    }
    return buffer;
}

#if LV_USE_TJPGD
typedef struct {
    const uint8_t *data;
    size_t size;
    size_t pos;
} fm_media_tjpgd_input_t;

typedef struct {
    uint8_t *pixels;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    TickType_t last_yield_tick;
} fm_media_tjpgd_output_t;

typedef struct {
    fm_media_tjpgd_input_t input;
    fm_media_tjpgd_output_t output;
} fm_media_tjpgd_session_t;

static size_t fm_media_tjpgd_input(JDEC *jd, uint8_t *buff, size_t ndata)
{
    fm_media_tjpgd_session_t *session = (fm_media_tjpgd_session_t *)jd->device;
    fm_media_tjpgd_input_t *input = session ? &session->input : NULL;
    size_t remaining;
    size_t take;

    if (!input || input->pos > input->size) {
        return 0;
    }
    remaining = input->size - input->pos;
    take = ndata < remaining ? ndata : remaining;
    if (buff && take > 0) {
        memcpy(buff, input->data + input->pos, take);
    }
    input->pos += take;
    return take;
}

static int fm_media_tjpgd_output(JDEC *jd, void *bitmap, JRECT *rect)
{
    fm_media_tjpgd_session_t *session = (fm_media_tjpgd_session_t *)jd->device;
    fm_media_tjpgd_output_t *output = session ? &session->output : NULL;
    const uint8_t *src = (const uint8_t *)bitmap;

    if (!output || !output->pixels || !src || !rect) {
        return 0;
    }
    TickType_t now = xTaskGetTickCount();
    if (now - output->last_yield_tick >= pdMS_TO_TICKS(40)) {
        output->last_yield_tick = now;
        vTaskDelay(1);
    }
    if (rect->right >= output->width || rect->bottom >= output->height) {
        ESP_LOGW(TAG,
                 "TJPGD output clipped: rect=%u,%u-%u,%u dst=%lux%lu",
                 rect->left,
                 rect->top,
                 rect->right,
                 rect->bottom,
                 (unsigned long)output->width,
                 (unsigned long)output->height);
        return 0;
    }

    for (uint32_t y = rect->top; y <= rect->bottom; y++) {
        uint8_t *dst = output->pixels + (size_t)y * output->stride + (size_t)rect->left * 2U;
        for (uint32_t x = rect->left; x <= rect->right; x++) {
            uint8_t b = src[0];
            uint8_t g = src[1];
            uint8_t r = src[2];
            uint16_t rgb565 = (uint16_t)(((uint16_t)(r & 0xF8U) << 8) |
                                          ((uint16_t)(g & 0xFCU) << 3) |
                                          ((uint16_t)b >> 3));
            dst[0] = (uint8_t)(rgb565 & 0xFFU);
            dst[1] = (uint8_t)(rgb565 >> 8);
            src += 3;
            dst += 2;
        }
    }
    return 1;
}

static uint8_t fm_media_tjpgd_scale(uint32_t width, uint32_t height)
{
    uint32_t area_w = 0;
    uint32_t area_h = 0;
    uint32_t target_w;
    uint32_t target_h;

    fm_media_get_target_area(&area_w, &area_h);
    target_w = fm_media_fit_dimension(width, height, area_w, area_h);
    target_h = (uint32_t)(((uint64_t)height * target_w) / width);
    if (target_h == 0) {
        target_h = 1;
    }

    for (int candidate = 3; candidate >= 0; candidate--) {
        uint32_t factor = 1U << (uint8_t)candidate;
        uint32_t out_w = (width + factor - 1U) >> (uint8_t)candidate;
        uint32_t out_h = (height + factor - 1U) >> (uint8_t)candidate;
        if (out_w >= target_w && out_h >= target_h) {
            return (uint8_t)candidate;
        }
    }
    return 0;
}

static esp_err_t fm_media_decode_jpeg_tjpgd(const char *path,
                                            const uint8_t *jpeg_buf,
                                            size_t jpeg_size,
                                            file_manager_image_t *image,
                                            char *status,
                                            size_t status_len)
{
    uint8_t work[4096];
    JDEC jd;
    fm_media_tjpgd_session_t session = {
        .input = {
            .data = jpeg_buf,
            .size = jpeg_size,
            .pos = 0,
        },
    };
    JRESULT res = jd_prepare(&jd, fm_media_tjpgd_input, work, sizeof(work), &session);

    if (res != JDR_OK || jd.width == 0 || jd.height == 0) {
        ESP_LOGW(TAG, "TJPGD prepare failed: %s res=%d", path, res);
        fm_media_set_status(status, status_len, TRS("JPEG 软件缩放解码准备失败"));
        return ESP_FAIL;
    }

    uint8_t scale = fm_media_tjpgd_scale(jd.width, jd.height);
    uint32_t out_w = (jd.width + ((1U << scale) - 1U)) >> scale;
    uint32_t out_h = (jd.height + ((1U << scale) - 1U)) >> scale;
    uint32_t area_w = 0;
    uint32_t area_h = 0;
    size_t stride = (size_t)out_w * 2U;
    size_t data_size = stride * out_h;
    uint8_t *pixels = fm_media_alloc_pixels(data_size);
    fm_media_get_target_area(&area_w, &area_h);

    ESP_LOGI(TAG,
             "TJPGD preview decode start: %s src=%ux%u area=%lux%lu scale=1/%u out=%lux%lu bytes=%lu",
             path,
             jd.width,
             jd.height,
             (unsigned long)area_w,
             (unsigned long)area_h,
             (unsigned)(1U << scale),
             (unsigned long)out_w,
             (unsigned long)out_h,
             (unsigned long)data_size);
    if (!pixels) {
        ESP_LOGW(TAG, "TJPGD preview alloc failed: %s bytes=%lu",
                 path,
                 (unsigned long)data_size);
        fm_media_set_status(status, status_len, TRS("JPEG 缩放预览内存不足"));
        return ESP_ERR_NO_MEM;
    }

    session.output = (fm_media_tjpgd_output_t) {
        .pixels = pixels,
        .width = out_w,
        .height = out_h,
        .stride = (uint32_t)stride,
        .last_yield_tick = xTaskGetTickCount(),
    };
    res = jd_decomp(&jd, fm_media_tjpgd_output, scale);
    if (res != JDR_OK) {
        free(pixels);
        ESP_LOGW(TAG, "TJPGD preview decode failed: %s res=%d", path, res);
        fm_media_set_status(status, status_len, TRS("JPEG 软件缩放解码失败"));
        return ESP_FAIL;
    }

    uint32_t final_w = fm_media_fit_dimension(out_w, out_h, area_w, area_h);
    uint32_t final_h = (uint32_t)(((uint64_t)out_h * final_w) / out_w);
    if (final_h == 0) {
        final_h = 1;
    }
    if ((final_w < out_w || final_h < out_h) && final_w > 0 && final_h > 0) {
        size_t final_stride = (size_t)final_w * 2U;
        size_t final_size = final_stride * final_h;
        bool used_ppa = false;
        uint8_t *final_pixels = fm_media_scale_rgb565_preview(pixels,
                                                              out_w,
                                                              out_h,
                                                              out_w,
                                                              final_w,
                                                              final_h,
                                                              &used_ppa,
                                                              "tjpgd-preview");
        if (final_pixels) {
            ESP_LOGI(TAG,
                     "TJPGD preview downscale: %s %lux%lu -> %lux%lu bytes=%lu method=%s",
                     path,
                     (unsigned long)out_w,
                     (unsigned long)out_h,
                     (unsigned long)final_w,
                     (unsigned long)final_h,
                     (unsigned long)final_size,
                     used_ppa ? "ppa" : "sw_bilinear");
            free(pixels);
            pixels = final_pixels;
            out_w = final_w;
            out_h = final_h;
            stride = final_stride;
            data_size = final_size;
        } else {
            ESP_LOGW(TAG,
                     "TJPGD preview downscale alloc failed: %s %lux%lu -> %lux%lu",
                     path,
                     (unsigned long)out_w,
                     (unsigned long)out_h,
                     (unsigned long)final_w,
                     (unsigned long)final_h);
        }
    }

    bool ppa_ready = file_manager_media_prepare_ppa(NULL, 0);
    image->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    image->dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    image->dsc.header.flags = 0;
    image->dsc.header.w = (uint16_t)out_w;
    image->dsc.header.h = (uint16_t)out_h;
    image->dsc.header.stride = (uint16_t)stride;
    image->dsc.data_size = data_size;
    image->dsc.data = pixels;
    image->source_w = out_w;
    image->source_h = out_h;
    image->hardware_jpeg = false;
    image->ppa_ready = ppa_ready;
    fm_media_set_status(status,
                        status_len,
                        ppa_ready ? TRS("JPEG 软件解码 + PPA 预览显示") :
                                    TRS("JPEG 软件解码预览显示"));
    ESP_LOGI(TAG,
             "TJPGD preview decode done: %s src=%ux%u out=%lux%lu bytes=%lu ppa=%d",
             path,
             jd.width,
             jd.height,
             (unsigned long)out_w,
             (unsigned long)out_h,
             (unsigned long)data_size,
             ppa_ready);
    return ESP_OK;
}
#endif

bool file_manager_media_prepare_ppa(char *status, size_t status_len)
{
#if CONFIG_SOC_PPA_SUPPORTED
    if (s_ppa_srm) {
        fm_media_set_status(status, status_len, TRS("PPA SRM 已就绪"));
        return true;
    }
    ppa_client_config_t cfg = {
        .oper_type = PPA_OPERATION_SRM,
    };
    esp_err_t err = ppa_register_client(&cfg, &s_ppa_srm);
    if (err != ESP_OK) {
        fm_media_set_status(status, status_len, esp_err_to_name(err));
        return false;
    }
    fm_media_set_status(status, status_len, TRS("PPA SRM 已就绪"));
    return true;
#else
    fm_media_set_status(status, status_len, TRS("当前芯片不支持 PPA"));
    return false;
#endif
}

esp_err_t file_manager_media_load_image(const char *path,
                                        file_manager_image_t *image,
                                        char *status,
                                        size_t status_len)
{
    if (!path || !image) {
        fm_media_set_status(status, status_len, TRS("图片请求无效"));
        return ESP_ERR_INVALID_ARG;
    }
    memset(image, 0, sizeof(*image));
    ESP_LOGI(TAG, "load image start: %s", path);

    if (fm_media_is_bmp(path)) {
        return fm_media_decode_bmp(path, image, status, status_len);
    }

    if (!fm_media_is_jpeg(path)) {
        ESP_LOGI(TAG, "image is not JPEG, use LVGL decoder: %s", path);
        fm_media_set_status(status, status_len, TRS("使用 LVGL PNG/TJPGD 解码器"));
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (web_file_manager_is_upload_active()) {
        ESP_LOGI(TAG, "web upload active, skip JPEG decode to protect WiFi DMA: %s", path);
        fm_media_set_status(status, status_len, TRS("上传中，暂不解码封面"));
        return ESP_ERR_NO_MEM;
    }

#if CONFIG_SOC_JPEG_DECODE_SUPPORTED
    size_t jpeg_size = 0;
    uint8_t *jpeg_buf = fm_media_read_file(path, &jpeg_size);
    if (!jpeg_buf) {
        ESP_LOGW(TAG, "read JPEG failed or too large: %s", path);
        fm_media_set_status(status, status_len, TRS("JPEG 文件过大或无法读取，改用 LVGL 解码器"));
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "JPEG file read: %s, %lu bytes", path, (unsigned long)jpeg_size);

    jpeg_decode_picture_info_t info = {0};
    uint32_t sof_w = 0;
    uint32_t sof_h = 0;
    uint8_t sof_marker = 0;
    esp_err_t sof_err = fm_media_jpeg_read_main_size(jpeg_buf, jpeg_size, &sof_w, &sof_h, &sof_marker);
    esp_err_t err = jpeg_decoder_get_info(jpeg_buf, (uint32_t)jpeg_size, &info);
    if ((err != ESP_OK || info.width == 0 || info.height == 0) &&
        (sof_err != ESP_OK || sof_w == 0 || sof_h == 0)) {
        free(jpeg_buf);
        ESP_LOGW(TAG, "JPEG get info failed: %s hw_err=%s hw=%lux%lu sof_err=%s sof=%lux%lu",
                 path,
                 esp_err_to_name(err),
                 (unsigned long)info.width,
                 (unsigned long)info.height,
                 esp_err_to_name(sof_err),
                 (unsigned long)sof_w,
                 (unsigned long)sof_h);
        fm_media_set_status(status, status_len, TRS("JPEG 头解析失败，改用 LVGL 解码器"));
        return err == ESP_OK ? sof_err : err;
    }
    uint32_t src_w = info.width;
    uint32_t src_h = info.height;
    if (sof_err == ESP_OK && sof_w > 0 && sof_h > 0) {
        src_w = sof_w;
        src_h = sof_h;
    }
    ESP_LOGI(TAG, "JPEG info: %s hw_info=%lux%lu main_sof=%lux%lu marker=0x%02X err=%s use=%lux%lu",
             path,
             (unsigned long)info.width,
             (unsigned long)info.height,
             (unsigned long)sof_w,
             (unsigned long)sof_h,
             (unsigned)sof_marker,
             esp_err_to_name(sof_err),
             (unsigned long)src_w,
             (unsigned long)src_h);

    uint32_t out_w = (src_w + 15U) & ~15U;
    uint32_t out_h = (src_h + 15U) & ~15U;
    uint32_t preview_w = fm_media_fit_dimension(src_w, src_h,
                                                FM_MEDIA_PREVIEW_MAX_W,
                                                FM_MEDIA_PREVIEW_MAX_H);
    uint32_t preview_h = fm_media_scaled_height(src_w, src_h, preview_w);
    size_t required_for_full = 0;
    size_t scratch_for_preview = 0;
    size_t psram_free = 0;
    size_t psram_largest = 0;
    bool direct_hw = fm_media_is_1080p_or_smaller(src_w, src_h);
    if (!direct_hw && !fm_media_should_use_full_hw_jpeg(src_w,
                                                        src_h,
                                                        out_w,
                                                        out_h,
                                                        preview_w,
                                                        preview_h,
                                                        &required_for_full,
                                                        &scratch_for_preview,
                                                        &psram_free,
                                                        &psram_largest)) {
        ESP_LOGI(TAG,
                 "JPEG full HW path too tight, use scaled decoder: %s full=%lu scratch=%lu required=%lu psram_free=%lu largest=%lu preview=%lux%lu",
                 path,
                 (unsigned long)((size_t)out_w * out_h * 2U),
                 (unsigned long)scratch_for_preview,
                 (unsigned long)required_for_full,
                 (unsigned long)psram_free,
                 (unsigned long)psram_largest,
                 (unsigned long)preview_w,
                 (unsigned long)preview_h);
#if LV_USE_TJPGD
        err = fm_media_decode_jpeg_tjpgd(path, jpeg_buf, jpeg_size, image, status, status_len);
        free(jpeg_buf);
        return err;
#else
        free(jpeg_buf);
        fm_media_set_status(status, status_len, TRS("JPEG 预览内存不足，未启用缩放解码器"));
        return ESP_ERR_NO_MEM;
#endif
    }
    if (direct_hw) {
        ESP_LOGI(TAG,
                 "JPEG <=1080p, use direct HW decoder: %s src=%lux%lu",
                 path,
                 (unsigned long)src_w,
                 (unsigned long)src_h);
    }
    if (!fm_media_dma_watermark_ok("jpeg-output")) {
#if LV_USE_TJPGD
        ESP_LOGI(TAG, "DMA tight before JPEG HW output, use TJPGD scaled decoder: %s", path);
        err = fm_media_decode_jpeg_tjpgd(path, jpeg_buf, jpeg_size, image, status, status_len);
        free(jpeg_buf);
        return err;
#else
        free(jpeg_buf);
        fm_media_set_status(status, status_len, TRS("DMA 内存紧张，暂不解码"));
        return ESP_ERR_NO_MEM;
#endif
    }
    size_t requested = (size_t)out_w * out_h * 2U;
    size_t out_alloc = 0;
    jpeg_decode_memory_alloc_cfg_t rx_mem_cfg = {
        .buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER,
    };
    uint8_t *pixels = (uint8_t *)jpeg_alloc_decoder_mem(requested, &rx_mem_cfg, &out_alloc);
    if (!pixels) {
        ESP_LOGW(TAG, "JPEG output alloc failed: %s requested=%lu",
                 path,
                 (unsigned long)requested);
#if LV_USE_TJPGD
        ESP_LOGI(TAG, "JPEG full output alloc failed, use TJPGD scaled decoder: %s", path);
        err = fm_media_decode_jpeg_tjpgd(path, jpeg_buf, jpeg_size, image, status, status_len);
        free(jpeg_buf);
        return err;
#endif
        free(jpeg_buf);
        fm_media_set_status(status, status_len, TRS("JPEG 输出内存不足，改用 LVGL 解码器"));
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "JPEG output buffer allocated: requested=%lu actual=%lu aligned=%lux%lu",
             (unsigned long)requested,
             (unsigned long)out_alloc,
             (unsigned long)out_w,
             (unsigned long)out_h);

    jpeg_decoder_handle_t decoder = NULL;
    jpeg_decode_engine_cfg_t engine_cfg = {
        .intr_priority = 0,
        .timeout_ms = 120,
    };
    jpeg_decode_cfg_t decode_cfg = {
        .output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
        .rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR,
        .conv_std = JPEG_YUV_RGB_CONV_STD_BT601,
    };
    uint32_t out_size = 0;

    if (!fm_media_dma_watermark_ok("jpeg-engine")) {
#if LV_USE_TJPGD
        ESP_LOGI(TAG, "DMA tight before JPEG HW engine, use TJPGD scaled decoder: %s", path);
        err = fm_media_decode_jpeg_tjpgd(path, jpeg_buf, jpeg_size, image, status, status_len);
        free(pixels);
        free(jpeg_buf);
        return err;
#else
        free(pixels);
        free(jpeg_buf);
        fm_media_set_status(status, status_len, TRS("JPEG 软件解码未启用，DMA 内存紧张"));
        return ESP_ERR_NO_MEM;
#endif
    }
    err = jpeg_new_decoder_engine(&engine_cfg, &decoder);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "JPEG decode process start: %s", path);
        err = jpeg_decoder_process(decoder,
                                   &decode_cfg,
                                   jpeg_buf,
                                   (uint32_t)jpeg_size,
                                   pixels,
                                   (uint32_t)out_alloc,
                                   &out_size);
    }
    if (decoder) {
        (void)jpeg_del_decoder_engine(decoder);
    }
    ESP_LOGI(TAG, "JPEG decode process done: %s err=%s out_size=%lu",
             path,
             esp_err_to_name(err),
             (unsigned long)out_size);

    if (err != ESP_OK || out_size == 0) {
        free(pixels);
#if LV_USE_TJPGD
        ESP_LOGW(TAG, "JPEG hardware decode failed, retry TJPGD scaled decoder: %s err=%s",
                 path,
                 esp_err_to_name(err));
        err = fm_media_decode_jpeg_tjpgd(path, jpeg_buf, jpeg_size, image, status, status_len);
        free(jpeg_buf);
        return err;
#else
        free(jpeg_buf);
        fm_media_set_status(status, status_len, TRS("JPEG 硬件解码失败，改用 LVGL 解码器"));
        return err == ESP_OK ? ESP_FAIL : err;
#endif
    }

    free(jpeg_buf);

    if ((size_t)out_w * out_h * 2U != out_size && out_w > 0) {
        out_h = out_size / (out_w * 2U);
    }
    if (out_w < src_w || out_h < src_h) {
        free(pixels);
        ESP_LOGW(TAG, "JPEG output size abnormal: src=%lux%lu out=%lux%lu",
                 (unsigned long)src_w,
                 (unsigned long)src_h,
                 (unsigned long)out_w,
                 (unsigned long)out_h);
        fm_media_set_status(status, status_len, TRS("JPEG 硬件解码尺寸异常，改用 LVGL 解码器"));
        return ESP_FAIL;
    }

    size_t compact_stride = (size_t)preview_w * 2U;
    size_t compact_size = compact_stride * preview_h;
    bool used_ppa = false;
    uint8_t *compact_pixels = fm_media_scale_rgb565_preview(pixels,
                                                            src_w,
                                                            src_h,
                                                            out_w,
                                                            preview_w,
                                                            preview_h,
                                                            &used_ppa,
                                                            "hw-jpeg-preview");
    if (!compact_pixels) {
        free(pixels);
        ESP_LOGW(TAG, "JPEG preview alloc failed: preview=%lux%lu bytes=%lu",
                 (unsigned long)preview_w,
                 (unsigned long)preview_h,
                 (unsigned long)compact_size);
        fm_media_set_status(status, status_len, TRS("JPEG 显示缓存不足，改用 LVGL 解码器"));
        return ESP_ERR_NO_MEM;
    }
    free(pixels);

    bool ppa_ready = file_manager_media_prepare_ppa(NULL, 0);
    image->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    image->dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    image->dsc.header.flags = 0;
    image->dsc.header.w = (uint16_t)preview_w;
    image->dsc.header.h = (uint16_t)preview_h;
    image->dsc.header.stride = (uint16_t)compact_stride;
    image->dsc.data_size = compact_size;
    image->dsc.data = compact_pixels;
    image->source_w = preview_w;
    image->source_h = preview_h;
    image->hardware_jpeg = true;
    image->ppa_ready = used_ppa;
    fm_media_set_status(status,
                        status_len,
                        used_ppa ? TRS("JPEG 硬件解码 + PPA 预览显示")
                                 : TRS("JPEG 硬件解码 + 软件缩放预览显示"));
    ESP_LOGI(TAG, "JPEG HW decoded %s: src=%lux%lu out=%lux%lu preview=%lux%lu compact=%lu bytes method=%s ppa_ready=%d",
             path,
             (unsigned long)src_w,
             (unsigned long)src_h,
             (unsigned long)out_w,
             (unsigned long)out_h,
             (unsigned long)preview_w,
             (unsigned long)preview_h,
             (unsigned long)compact_size,
             used_ppa ? "ppa" : "sw_bilinear",
             ppa_ready);
    return ESP_OK;
#else
    fm_media_set_status(status, status_len, TRS("未启用 JPEG 硬件解码器"));
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t file_manager_media_load_image_for_area(const char *path,
                                                 uint32_t area_w,
                                                 uint32_t area_h,
                                                 file_manager_image_t *image,
                                                 char *status,
                                                 size_t status_len)
{
    esp_err_t err;

    if (!s_target_area_lock) {
        s_target_area_lock = xSemaphoreCreateMutex();
    }
    if (s_target_area_lock) {
        xSemaphoreTake(s_target_area_lock, portMAX_DELAY);
    }
    fm_media_set_target_area(area_w, area_h);
    err = file_manager_media_load_image(path, image, status, status_len);
    fm_media_clear_target_area();
    if (s_target_area_lock) {
        xSemaphoreGive(s_target_area_lock);
    }
    return err;
}

void file_manager_media_release_image(file_manager_image_t *image)
{
    if (!image) {
        return;
    }
    if (image->dsc.data) {
        free((void *)image->dsc.data);
    }
    memset(image, 0, sizeof(*image));
}

void file_manager_media_audio_play_reserved(const char *path)
{
    ESP_LOGI(TAG, "audio playback reserved: %s", path ? path : "");
}
