/**
 * @file display_st7789.h
 * @brief ST7789 Display Driver for ESP32-C6
 * 
 * Simple display driver for 172x320 ST7789 TFT displays
 * Provides basic text rendering and display control functions
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_lcd_panel_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

// Display configuration - Landscape mode (320x172)
#define DISPLAY_WIDTH           320
#define DISPLAY_HEIGHT          172
#define DISPLAY_PIXEL_CLOCK_HZ  (80 * 1000 * 1000)
#define DISPLAY_CMD_BITS        8
#define DISPLAY_PARAM_BITS      8

// GPIO pin definitions (matching working example)
#define DISPLAY_PIN_MOSI        GPIO_NUM_6
#define DISPLAY_PIN_SCLK        GPIO_NUM_7
#define DISPLAY_PIN_CS          GPIO_NUM_14
#define DISPLAY_PIN_DC          GPIO_NUM_15
#define DISPLAY_PIN_RST         GPIO_NUM_21  
#define DISPLAY_PIN_BL          GPIO_NUM_22
#define DISPLAY_BL_ON_LEVEL     1

// Display offset for ST7789 (landscape mode)
#define DISPLAY_OFFSET_X        0
#define DISPLAY_OFFSET_Y        34

// Colors (RGB565 format)
#define COLOR_BLACK             0x0000
#define COLOR_WHITE             0xFFFF
#define COLOR_RED               0xF800
#define COLOR_GREEN             0x07E0
#define COLOR_BLUE              0x001F
#define COLOR_YELLOW            0xFFE0
#define COLOR_CYAN              0x07FF
#define COLOR_MAGENTA           0xF81F

// Font configuration
#define FONT_WIDTH              8
#define FONT_HEIGHT             16
#define DISPLAY_MAX_CHARS_PER_LINE  (DISPLAY_WIDTH / FONT_WIDTH)
#define DISPLAY_MAX_LINES           (DISPLAY_HEIGHT / FONT_HEIGHT)

/**
 * @brief Display configuration structure
 */
typedef struct {
    int pin_mosi;       /**< MOSI pin */
    int pin_sclk;       /**< SCLK pin */
    int pin_cs;         /**< CS pin */
    int pin_dc;         /**< DC pin */
    int pin_rst;        /**< Reset pin */
    int pin_bl;         /**< Backlight pin */
    int bl_on_level;    /**< Backlight on level (0 or 1) */
    uint32_t pixel_clock_hz;  /**< Pixel clock frequency */
} display_config_t;

/**
 * @brief Display handle
 */
typedef struct {
    esp_lcd_panel_handle_t panel_handle;
    bool initialized;
    uint16_t width;
    uint16_t height;
    int backlight_pin;
    int backlight_on_level;
} display_handle_t;

/**
 * @brief Initialize the display
 * 
 * @param config Display configuration
 * @param display_handle Pointer to display handle
 * @return esp_err_t ESP_OK on success
 */
esp_err_t display_init(const display_config_t *config, display_handle_t *display_handle);

/**
 * @brief Deinitialize the display
 * 
 * @param display_handle Display handle
 * @return esp_err_t ESP_OK on success
 */
esp_err_t display_deinit(display_handle_t *display_handle);

/**
 * @brief Turn on/off the display backlight
 * 
 * @param display_handle Display handle
 * @param on true to turn on, false to turn off
 * @return esp_err_t ESP_OK on success
 */
esp_err_t display_backlight_set(display_handle_t *display_handle, bool on);

/**
 * @brief Clear the display with specified color
 * 
 * @param display_handle Display handle
 * @param color Color to fill (RGB565 format)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t display_clear(display_handle_t *display_handle, uint16_t color);

/**
 * @brief Draw a pixel at specified coordinates
 * 
 * @param display_handle Display handle
 * @param x X coordinate
 * @param y Y coordinate
 * @param color Pixel color (RGB565 format)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t display_draw_pixel(display_handle_t *display_handle, int x, int y, uint16_t color);

/**
 * @brief Fill a rectangle with specified color
 * 
 * @param display_handle Display handle
 * @param x X coordinate of top-left corner
 * @param y Y coordinate of top-left corner
 * @param width Rectangle width
 * @param height Rectangle height
 * @param color Fill color (RGB565 format)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t display_fill_rect(display_handle_t *display_handle, int x, int y, int width, int height, uint16_t color);

/**
 * @brief Draw a character at specified position
 * 
 * @param display_handle Display handle
 * @param x X coordinate
 * @param y Y coordinate
 * @param character Character to draw
 * @param fg_color Foreground color (RGB565 format)
 * @param bg_color Background color (RGB565 format)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t display_draw_char(display_handle_t *display_handle, int x, int y, char character, uint16_t fg_color, uint16_t bg_color);

/**
 * @brief Draw a string at specified position
 * 
 * @param display_handle Display handle
 * @param x X coordinate
 * @param y Y coordinate
 * @param str String to draw
 * @param fg_color Foreground color (RGB565 format)
 * @param bg_color Background color (RGB565 format)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t display_draw_string(display_handle_t *display_handle, int x, int y, const char *str, uint16_t fg_color, uint16_t bg_color);

/**
 * @brief Print text to display (printf-style)
 * 
 * @param display_handle Display handle
 * @param x X coordinate
 * @param y Y coordinate
 * @param fg_color Foreground color (RGB565 format)
 * @param bg_color Background color (RGB565 format)
 * @param format Printf-style format string
 * @param ... Arguments for format string
 * @return esp_err_t ESP_OK on success
 */
esp_err_t display_printf(display_handle_t *display_handle, int x, int y, uint16_t fg_color, uint16_t bg_color, const char *format, ...);

/**
 * @brief Get default display configuration
 * 
 * @param config Pointer to configuration structure to fill
 */
void display_get_default_config(display_config_t *config);

/**
 * @brief Convert RGB888 to RGB565 format
 * 
 * @param r Red component (0-255)
 * @param g Green component (0-255)
 * @param b Blue component (0-255)
 * @return uint16_t RGB565 color value
 */
static inline uint16_t display_rgb888_to_rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

/**
 * @brief Draw window with color buffer for LVGL integration
 * 
 * @param x_start Start X coordinate
 * @param y_start Start Y coordinate  
 * @param x_end End X coordinate
 * @param y_end End Y coordinate
 * @param color Pointer to color buffer (RGB565 format)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t lcd_add_window(uint16_t x_start, uint16_t y_start, uint16_t x_end, uint16_t y_end, uint16_t* color);

#ifdef __cplusplus
}
#endif