
set(M5STACK_TAB5_REQUIRES
    driver
    esp_lcd
    esp_codec_dev
    esp_lcd_ili9881c
    esp_lcd_touch_st7123
    esp_lcd_st7121
)

set(M5STACK_TAB5_USE_GRAPHIC_LIB ON)
if(M5STACK_TAB5_FORCE_GRAPHIC_LIB)
    set(M5STACK_TAB5_USE_GRAPHIC_LIB ON)
elseif(M5STACK_TAB5_NO_GRAPHIC_LIB OR BSP_CONFIG_NO_GRAPHIC_LIB)
    set(M5STACK_TAB5_USE_GRAPHIC_LIB OFF)
elseif(NOT M5STACK_TAB5_FORCE_GRAPHIC_LIB
       AND (CMAKE_SOURCE_DIR MATCHES "[/\\\\]Update_project([/\\\\]|$)"
            OR CMAKE_BINARY_DIR MATCHES "[/\\\\]Update_project([/\\\\]|$)"))
    set(M5STACK_TAB5_USE_GRAPHIC_LIB OFF)
endif()

if(M5STACK_TAB5_USE_GRAPHIC_LIB)
    list(APPEND M5STACK_TAB5_REQUIRES
        lvgl
        esp_lvgl_port
    )
endif()

idf_component_register(
    SRCS "m5stack_tab5.c" "esp_lcd_st7123.c"
    INCLUDE_DIRS "include"
    INCLUDE_DIRS "include/bsp"
    PRIV_INCLUDE_DIRS "priv_include"
    REQUIRES ${M5STACK_TAB5_REQUIRES}
    PRIV_REQUIRES usb spiffs fatfs
)

if(NOT M5STACK_TAB5_USE_GRAPHIC_LIB)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC BSP_CONFIG_NO_GRAPHIC_LIB=1)
else()
    if(TARGET idf::lvgl)
        target_link_libraries(${COMPONENT_LIB} PUBLIC idf::lvgl)
    endif()
    if(TARGET idf::esp_lvgl_port)
        target_link_libraries(${COMPONENT_LIB} PUBLIC idf::esp_lvgl_port)
    elseif(TARGET idf::espressif__esp_lvgl_port)
        target_link_libraries(${COMPONENT_LIB} PUBLIC idf::espressif__esp_lvgl_port)
    endif()
endif()
