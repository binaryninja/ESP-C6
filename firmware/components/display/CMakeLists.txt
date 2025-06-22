idf_component_register(SRCS "display_st7789.c" "lvgl_driver.c"
                       INCLUDE_DIRS "include"
                       REQUIRES driver esp_lcd spi_flash esp_timer freertos esp_common lvgl)
