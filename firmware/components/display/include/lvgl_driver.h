#pragma once

#include <lvgl.h>
#include <esp_heap_caps.h>
#include "display_st7789.h"

#define LVGL_WIDTH    320
#define LVGL_HEIGHT   172
#define LVGL_BUF_LEN  (LVGL_WIDTH * LVGL_HEIGHT / 10)

#define EXAMPLE_LVGL_TICK_PERIOD_MS  5

#ifdef __cplusplus
extern "C" {
#endif

void lvgl_print(const char * buf);
void lvgl_display_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p);
void lvgl_touchpad_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data);
void example_increase_lvgl_tick(void *arg);

void lvgl_init(void);
void lvgl_timer_loop(void);

#ifdef __cplusplus
}
#endif