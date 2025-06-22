/**
 * @file lvgl_driver.c
 * @brief LVGL Driver Implementation for ESP32-C6 with ST7789 Display
 * 
 * Based on working example LVGL_Driver.cpp
 * Implements LVGL integration with ST7789 display driver
 */

#include "lvgl_driver.h"
#include "display_st7789.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "LVGL_DRIVER";

static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf1[LVGL_BUF_LEN];
static lv_color_t buf2[LVGL_BUF_LEN];

/* Serial debugging */
void lvgl_print(const char * buf)
{
    ESP_LOGI(TAG, "%s", buf);
}

/**
 * @brief Display flushing callback for LVGL
 * This function implements associating LVGL data to the LCD screen
 */
void lvgl_display_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p)
{
    // Call our ST7789 driver to update the display area
    lcd_add_window(area->x1, area->y1, area->x2, area->y2, (uint16_t *)&color_p->full);
    
    // Tell LVGL that flushing is done
    lv_disp_flush_ready(disp_drv);
}

/**
 * @brief Read the touchpad (dummy implementation)
 */
void lvgl_touchpad_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data)
{
    // No touchpad on this display
    data->state = LV_INDEV_STATE_REL;
}

/**
 * @brief LVGL tick timer callback
 */
void example_increase_lvgl_tick(void *arg)
{
    /* Tell LVGL how many milliseconds has elapsed */
    lv_tick_inc(EXAMPLE_LVGL_TICK_PERIOD_MS);
}

/**
 * @brief Initialize LVGL with display and input drivers
 */
void lvgl_init(void)
{
    ESP_LOGI(TAG, "Initializing LVGL");
    
    // Initialize LVGL
    lv_init();
    
    // Initialize display buffer
    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, LVGL_BUF_LEN);

    // Initialize the display driver
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    
    // Set display resolution
    disp_drv.hor_res = LVGL_WIDTH;
    disp_drv.ver_res = LVGL_HEIGHT;
    disp_drv.flush_cb = lvgl_display_flush;
    disp_drv.full_refresh = 1;  // Always make the whole screen redrawn
    disp_drv.draw_buf = &draw_buf;
    
    // Register the display driver
    lv_disp_drv_register(&disp_drv);

    // Initialize the input device driver (dummy touchpad)
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = lvgl_touchpad_read;
    lv_indev_drv_register(&indev_drv);

    // Create simple label for testing
    lv_obj_t *label = lv_label_create(lv_scr_act());
    lv_label_set_text(label, "ESP32-C6 LVGL");
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);

    // Create LVGL tick timer
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &example_increase_lvgl_tick,
        .name = "lvgl_tick"
    };
    esp_timer_handle_t lvgl_tick_timer = NULL;
    esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer);
    esp_timer_start_periodic(lvgl_tick_timer, EXAMPLE_LVGL_TICK_PERIOD_MS * 1000);

    ESP_LOGI(TAG, "LVGL initialized successfully");
}

/**
 * @brief LVGL timer handler - should be called periodically
 * Returns the time until next timer execution in milliseconds
 */
void lvgl_timer_loop(void)
{
    uint32_t task_delay_ms = lv_timer_handler(); /* let the GUI do its work */
    
    // If LVGL suggests a delay, yield to other tasks for a minimum time
    if (task_delay_ms == LV_NO_TIMER_READY) {
        // No timers ready, yield for default period
        vTaskDelay(pdMS_TO_TICKS(5));
    } else if (task_delay_ms > 0) {
        // LVGL suggests a specific delay, but cap it to prevent blocking too long
        uint32_t delay = (task_delay_ms > 100) ? 100 : task_delay_ms;
        vTaskDelay(pdMS_TO_TICKS(delay));
    }
    // If task_delay_ms is 0, continue immediately (timer ready)
}