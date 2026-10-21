idf_component_register(
    SRCS
        "updater_main.c"
        "updater_sdcard.c"
        "updater_ui.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        app_update
        bootloader_support
        esp_app_format
        esp_lcd
        esp_partition
        esp_timer
        fatfs
        m5stack_tab5
        nvs_flash
        sdmmc
        spi_flash
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-bidi-chars)
