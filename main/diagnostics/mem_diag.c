#include "diagnostics/mem_diag.h"

#include <stddef.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "mem_diag";

typedef struct {
    size_t dma_free;
    size_t internal_free;
    size_t psram_free;
    bool valid;
} mem_diag_snapshot_t;

static mem_diag_snapshot_t s_last;
static portMUX_TYPE s_last_mux = portMUX_INITIALIZER_UNLOCKED;

static long mem_diag_delta(size_t current, size_t previous)
{
    return (long)current - (long)previous;
}

void mem_diag_checkpoint_reset(void)
{
    portENTER_CRITICAL(&s_last_mux);
    s_last = (mem_diag_snapshot_t) {0};
    portEXIT_CRITICAL(&s_last_mux);
}

void mem_diag_checkpoint(const char *label)
{
    size_t dma_free = heap_caps_get_free_size(MALLOC_CAP_DMA);
    size_t dma_largest = heap_caps_get_largest_free_block(MALLOC_CAP_DMA);
    size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t internal_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    size_t psram_largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    mem_diag_snapshot_t previous;
    mem_diag_snapshot_t current = {
        .dma_free = dma_free,
        .internal_free = internal_free,
        .psram_free = psram_free,
        .valid = true,
    };

    portENTER_CRITICAL(&s_last_mux);
    previous = s_last;
    s_last = current;
    portEXIT_CRITICAL(&s_last_mux);

    if (previous.valid) {
        ESP_LOGI(TAG,
                 "[%s] DMA free=%u largest=%u delta=%ld | internal free=%u largest=%u delta=%ld | psram free=%u largest=%u delta=%ld",
                 label ? label : "checkpoint",
                 (unsigned)dma_free,
                 (unsigned)dma_largest,
                 mem_diag_delta(dma_free, previous.dma_free),
                 (unsigned)internal_free,
                 (unsigned)internal_largest,
                 mem_diag_delta(internal_free, previous.internal_free),
                 (unsigned)psram_free,
                 (unsigned)psram_largest,
                 mem_diag_delta(psram_free, previous.psram_free));
    } else {
        ESP_LOGI(TAG,
                 "[%s] DMA free=%u largest=%u | internal free=%u largest=%u | psram free=%u largest=%u",
                 label ? label : "checkpoint",
                 (unsigned)dma_free,
                 (unsigned)dma_largest,
                 (unsigned)internal_free,
                 (unsigned)internal_largest,
                 (unsigned)psram_free,
                 (unsigned)psram_largest);
    }

}
