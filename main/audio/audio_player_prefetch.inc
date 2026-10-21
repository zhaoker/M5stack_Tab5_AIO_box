static void audio_player_prefetch_task(void *arg)
{
    audio_player_prefetch_t *prefetch = (audio_player_prefetch_t *)arg;

    while (prefetch && prefetch->fp && prefetch->buffer) {
        size_t space = 0;
        size_t write_pos = 0;
        size_t to_read = 0;
        size_t low_watermark = 0;

        xSemaphoreTake(prefetch->lock, portMAX_DELAY);
        if (prefetch->stop || prefetch->eof || prefetch->error) {
            xSemaphoreGive(prefetch->lock);
            break;
        }
        space = prefetch->capacity - prefetch->fill;
        write_pos = prefetch->write_pos;
        low_watermark = prefetch->capacity / 2U;
        if (!prefetch->refill_active && prefetch->fill <= low_watermark) {
            prefetch->refill_active = true;
        }
        if (space == 0) {
            prefetch->refill_active = false;
        } else if (prefetch->refill_active && space >= AUDIO_PLAYER_PREFETCH_READ_CHUNK_BYTES) {
            to_read = space;
            if (to_read > AUDIO_PLAYER_PREFETCH_READ_CHUNK_BYTES) {
                to_read = AUDIO_PLAYER_PREFETCH_READ_CHUNK_BYTES;
            }
            size_t contiguous = prefetch->capacity - write_pos;
            if (to_read > contiguous) {
                to_read = contiguous;
            }
        } else if (prefetch->refill_active && space < AUDIO_PLAYER_PREFETCH_READ_CHUNK_BYTES) {
            prefetch->refill_active = false;
        }
        xSemaphoreGive(prefetch->lock);

        if (to_read == 0) {
            (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(20));
            continue;
        }

        size_t got = fread(prefetch->buffer + write_pos, 1, to_read, prefetch->fp);
        bool eof = got < to_read && feof(prefetch->fp);
        bool error = got == 0 && ferror(prefetch->fp);

        xSemaphoreTake(prefetch->lock, portMAX_DELAY);
        if (got > 0) {
            prefetch->write_pos = (prefetch->write_pos + got) % prefetch->capacity;
            prefetch->fill += got;
            prefetch->bytes_read += got;
        }
        if (prefetch->fill >= prefetch->capacity) {
            prefetch->refill_active = false;
        }
        if (eof || got == 0) {
            prefetch->eof = true;
        }
        if (error) {
            prefetch->error = true;
        }
        bool should_stop = prefetch->stop || prefetch->error || (prefetch->eof && got == 0);
        xSemaphoreGive(prefetch->lock);

        if (should_stop) {
            break;
        }
    }

    if (prefetch) {
        ESP_LOGI(TAG,
                 "audio prefetch task exit: read=%llu eof=%u error=%u",
                 (unsigned long long)prefetch->bytes_read,
                 prefetch->eof ? 1U : 0U,
                 prefetch->error ? 1U : 0U);
        prefetch->task = NULL;
    }
    vTaskDeleteWithCaps(NULL);
}

static esp_err_t audio_player_prefetch_start(audio_player_prefetch_t *prefetch,
                                             FILE *fp,
                                             const char *format_name)
{
    if (!prefetch || !fp) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(prefetch, 0, sizeof(*prefetch));
    prefetch->fp = fp;
    prefetch->lock = xSemaphoreCreateMutex();
    if (!prefetch->lock) {
        return ESP_ERR_NO_MEM;
    }

    for (size_t bytes = AUDIO_PLAYER_PREFETCH_MAX_BYTES;
         bytes >= AUDIO_PLAYER_PREFETCH_MIN_BYTES;
         bytes >>= 1) {
        prefetch->buffer = (uint8_t *)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (prefetch->buffer) {
            prefetch->capacity = bytes;
            break;
        }
    }
    if (!prefetch->buffer) {
        vSemaphoreDelete(prefetch->lock);
        memset(prefetch, 0, sizeof(*prefetch));
        return ESP_ERR_NO_MEM;
    }

    BaseType_t task_ret = xTaskCreateWithCaps(audio_player_prefetch_task,
                                              "aud_prefetch",
                                              AUDIO_PLAYER_PREFETCH_TASK_STACK,
                                              prefetch,
                                              AUDIO_PLAYER_PREFETCH_TASK_PRIORITY,
                                              &prefetch->task,
                                              MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (task_ret != pdPASS) {
        free(prefetch->buffer);
        vSemaphoreDelete(prefetch->lock);
        memset(prefetch, 0, sizeof(*prefetch));
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG,
             "ADF %s prefetch started: cache=%u read_chunk=%u",
             format_name ? format_name : "audio",
             (unsigned)prefetch->capacity,
             (unsigned)AUDIO_PLAYER_PREFETCH_READ_CHUNK_BYTES);
    return ESP_OK;
}

static int audio_player_prefetch_read(audio_player_prefetch_t *prefetch,
                                      char *buf,
                                      int len,
                                      volatile bool *abort_requested)
{
    size_t copied = 0;

    if (!prefetch || !prefetch->buffer || !buf || len <= 0) {
        return AEL_IO_FAIL;
    }

    while (copied < (size_t)len) {
        size_t available = 0;
        size_t read_pos = 0;
        bool eof = false;
        bool error = false;
        bool stopped = false;

        if (abort_requested && *abort_requested) {
            return copied > 0 ? (int)copied : AEL_IO_ABORT;
        }

        xSemaphoreTake(prefetch->lock, portMAX_DELAY);
        available = prefetch->fill;
        read_pos = prefetch->read_pos;
        eof = prefetch->eof;
        error = prefetch->error;
        stopped = prefetch->stop;
        if (available > 0) {
            size_t take = (size_t)len - copied;
            if (take > available) {
                take = available;
            }
            size_t contiguous = prefetch->capacity - read_pos;
            if (take > contiguous) {
                take = contiguous;
            }
            memcpy(buf + copied, prefetch->buffer + read_pos, take);
            prefetch->read_pos = (prefetch->read_pos + take) % prefetch->capacity;
            prefetch->fill -= take;
            copied += take;
            xSemaphoreGive(prefetch->lock);
            if (prefetch->task) {
                xTaskNotifyGive(prefetch->task);
            }
            continue;
        }
        xSemaphoreGive(prefetch->lock);

        if (copied > 0) {
            return (int)copied;
        }
        if (error) {
            return AEL_IO_FAIL;
        }
        if (eof) {
            return AEL_IO_DONE;
        }
        if (stopped) {
            return AEL_IO_ABORT;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    return (int)copied;
}

static void audio_player_prefetch_stop(audio_player_prefetch_t *prefetch)
{
    if (!prefetch || !prefetch->lock) {
        return;
    }

    xSemaphoreTake(prefetch->lock, portMAX_DELAY);
    prefetch->stop = true;
    xSemaphoreGive(prefetch->lock);
    if (prefetch->task) {
        xTaskNotifyGive(prefetch->task);
    }
    while (prefetch->task) {
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    free(prefetch->buffer);
    prefetch->buffer = NULL;
    vSemaphoreDelete(prefetch->lock);
    prefetch->lock = NULL;
    memset(prefetch, 0, sizeof(*prefetch));
}
