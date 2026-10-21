
idf_component_register(
    SRCS "m5stack_tab5.c" "esp_lcd_st7123.c"
    INCLUDE_DIRS "include"
    INCLUDE_DIRS "include/bsp"
    PRIV_INCLUDE_DIRS "priv_include"
    REQUIRES 
        driver 
        esp_lcd
        esp_lcd_ili9881c
        esp_lcd_st7121
    PRIV_REQUIRES spiffs fatfs
)

target_compile_definitions(${COMPONENT_LIB} PUBLIC BSP_CONFIG_NO_AUDIO=1)
target_compile_definitions(${COMPONENT_LIB} PUBLIC BSP_CONFIG_NO_TOUCH=1)
