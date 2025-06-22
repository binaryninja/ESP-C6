/**
 * @file display_st7789.c
 * @brief ST7789 Display Driver Implementation for ESP32-C6
 * 
 * Based on working ST7789 example with proper initialization sequence
 * Uses direct SPI commands for reliable communication
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "display_st7789.h"

static const char *TAG = "DISPLAY_ST7789";

// SPI device handle
static spi_device_handle_t spi_device = NULL;
static bool display_ready = false;

// Current display orientation and offsets
static int offset_x = DISPLAY_OFFSET_X;
static int offset_y = DISPLAY_OFFSET_Y;

// No font needed - we'll draw simple solid blocks

/**
 * @brief Send command to ST7789
 */
static esp_err_t lcd_write_command(uint8_t cmd)
{
    spi_transaction_t trans = {
        .length = 8,
        .tx_buffer = &cmd,
        .user = (void*)0, // DC = 0 for command
    };
    return spi_device_polling_transmit(spi_device, &trans);
}

/**
 * @brief Send data to ST7789
 */
static esp_err_t lcd_write_data(uint8_t data)
{
    spi_transaction_t trans = {
        .length = 8,
        .tx_buffer = &data,
        .user = (void*)1, // DC = 1 for data
    };
    return spi_device_polling_transmit(spi_device, &trans);
}

/**
 * @brief Send 16-bit data to ST7789
 */
static esp_err_t lcd_write_data_word(uint16_t data)
{
    uint8_t data_bytes[2] = {(data >> 8) & 0xFF, data & 0xFF};
    spi_transaction_t trans = {
        .length = 16,
        .tx_buffer = data_bytes,
        .user = (void*)1, // DC = 1 for data
    };
    return spi_device_polling_transmit(spi_device, &trans);
}

/**
 * @brief Send multiple bytes of data to ST7789
 */
static esp_err_t lcd_write_data_nbytes(const uint8_t* data, size_t len)
{
    if (len == 0) return ESP_OK;
    
    spi_transaction_t trans = {
        .length = len * 8,
        .tx_buffer = data,
        .user = (void*)1, // DC = 1 for data
    };
    return spi_device_polling_transmit(spi_device, &trans);
}

/**
 * @brief SPI pre-transfer callback to set DC line
 */
static void lcd_spi_pre_transfer_callback(spi_transaction_t *t)
{
    int dc_level = (int)t->user;
    gpio_set_level(DISPLAY_PIN_DC, dc_level);
}

/**
 * @brief Reset the ST7789 display
 */
static void lcd_reset(void)
{
    gpio_set_level(DISPLAY_PIN_CS, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(DISPLAY_PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(DISPLAY_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
}

/**
 * @brief Set display orientation
 */
static void set_orientation(uint8_t orientation)
{
    uint8_t madctl = 0;

    switch (orientation) {
        case 0: // Portrait
            madctl = 0x00;
            offset_x = 34;
            offset_y = 0;
            break;
        case 1: // Landscape (default)
            madctl = 0x60;
            offset_x = 0;
            offset_y = 34;
            break;
        case 2: // Inverted Portrait
            madctl = 0xC0;
            offset_x = 34;
            offset_y = 0;
            break;
        case 3: // Inverted Landscape
            madctl = 0xA0;
            offset_x = 0;
            offset_y = 34;
            break;
        default:
            return;
    }

    lcd_write_command(0x36);
    lcd_write_data(madctl);
}

/**
 * @brief Initialize ST7789 with proper sequence
 */
static esp_err_t lcd_init_sequence(void)
{
    ESP_LOGI(TAG, "Starting ST7789 initialization sequence");
    
    lcd_reset();
    
    // Sleep Out
    lcd_write_command(0x11);
    vTaskDelay(pdMS_TO_TICKS(120));
    
    // Memory Access Control
    lcd_write_command(0x36);
    set_orientation(1); // Landscape mode
    
    // Pixel Format Set
    lcd_write_command(0x3A);
    lcd_write_data(0x05); // 16-bit RGB565
    
    // Interface Control
    lcd_write_command(0xB0);
    lcd_write_data(0x00);
    lcd_write_data(0xE8);
    
    // Porch Setting
    lcd_write_command(0xB2);
    lcd_write_data(0x0C);
    lcd_write_data(0x0C);
    lcd_write_data(0x00);
    lcd_write_data(0x33);
    lcd_write_data(0x33);
    
    // Gate Control
    lcd_write_command(0xB7);
    lcd_write_data(0x35);
    
    // VCOM Setting
    lcd_write_command(0xBB);
    lcd_write_data(0x35);
    
    // LCM Control
    lcd_write_command(0xC0);
    lcd_write_data(0x2C);
    
    // VDV and VRH Command Enable
    lcd_write_command(0xC2);
    lcd_write_data(0x01);
    
    // VRH Set
    lcd_write_command(0xC3);
    lcd_write_data(0x13);
    
    // VDV Set
    lcd_write_command(0xC4);
    lcd_write_data(0x20);
    
    // Frame Rate Control
    lcd_write_command(0xC6);
    lcd_write_data(0x0F);
    
    // Power Control 1
    lcd_write_command(0xD0);
    lcd_write_data(0xA4);
    lcd_write_data(0xA1);
    
    // Power Control 2
    lcd_write_command(0xD6);
    lcd_write_data(0xA1);
    
    // Positive Voltage Gamma Control
    lcd_write_command(0xE0);
    lcd_write_data(0xF0);
    lcd_write_data(0x00);
    lcd_write_data(0x04);
    lcd_write_data(0x04);
    lcd_write_data(0x04);
    lcd_write_data(0x05);
    lcd_write_data(0x29);
    lcd_write_data(0x33);
    lcd_write_data(0x3E);
    lcd_write_data(0x38);
    lcd_write_data(0x12);
    lcd_write_data(0x12);
    lcd_write_data(0x28);
    lcd_write_data(0x30);
    
    // Negative Voltage Gamma Control
    lcd_write_command(0xE1);
    lcd_write_data(0xF0);
    lcd_write_data(0x07);
    lcd_write_data(0x0A);
    lcd_write_data(0x0D);
    lcd_write_data(0x0B);
    lcd_write_data(0x07);
    lcd_write_data(0x28);
    lcd_write_data(0x33);
    lcd_write_data(0x3E);
    lcd_write_data(0x36);
    lcd_write_data(0x14);
    lcd_write_data(0x14);
    lcd_write_data(0x29);
    lcd_write_data(0x32);
    
    // Display Inversion On
    lcd_write_command(0x21);
    
    // Sleep Out
    lcd_write_command(0x11);
    vTaskDelay(pdMS_TO_TICKS(120));
    
    // Display On
    lcd_write_command(0x29);
    
    ESP_LOGI(TAG, "ST7789 initialization sequence completed");
    return ESP_OK;
}

/**
 * @brief Set cursor position for drawing
 */
static esp_err_t lcd_set_cursor(uint16_t x_start, uint16_t y_start, uint16_t x_end, uint16_t y_end)
{
    // Column Address Set
    lcd_write_command(0x2A);
    lcd_write_data((x_start + offset_x) >> 8);
    lcd_write_data((x_start + offset_x) & 0xFF);
    lcd_write_data((x_end + offset_x) >> 8);
    lcd_write_data((x_end + offset_x) & 0xFF);
    
    // Row Address Set
    lcd_write_command(0x2B);
    lcd_write_data((y_start + offset_y) >> 8);
    lcd_write_data((y_start + offset_y) & 0xFF);
    lcd_write_data((y_end + offset_y) >> 8);
    lcd_write_data((y_end + offset_y) & 0xFF);
    
    // Memory Write
    lcd_write_command(0x2C);
    
    return ESP_OK;
}

/**
 * @brief Initialize backlight PWM
 */
static esp_err_t backlight_init(void)
{
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .freq_hz = 1000,
        .clk_cfg = LEDC_AUTO_CLK
    };
    esp_err_t ret = ledc_timer_config(&ledc_timer);
    if (ret != ESP_OK) return ret;
    
    ledc_channel_config_t ledc_channel = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = DISPLAY_PIN_BL,
        .duty = 1000, // 100% brightness
        .hpoint = 0
    };
    return ledc_channel_config(&ledc_channel);
}

/**
 * @brief Set backlight brightness
 */
static esp_err_t set_backlight(uint8_t brightness)
{
    if (brightness > 100) brightness = 100;
    uint32_t duty = (brightness * 1023) / 100;
    return ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty) ||
           ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

void display_get_default_config(display_config_t *config)
{
    if (!config) return;
    
    config->pin_mosi = DISPLAY_PIN_MOSI;
    config->pin_sclk = DISPLAY_PIN_SCLK;
    config->pin_cs = DISPLAY_PIN_CS;
    config->pin_dc = DISPLAY_PIN_DC;
    config->pin_rst = DISPLAY_PIN_RST;
    config->pin_bl = DISPLAY_PIN_BL;
    config->bl_on_level = DISPLAY_BL_ON_LEVEL;
    config->pixel_clock_hz = DISPLAY_PIXEL_CLOCK_HZ;
}

esp_err_t display_init(const display_config_t *config, display_handle_t *display_handle)
{
    ESP_LOGI(TAG, "Initializing ST7789 display");
    
    if (!config || !display_handle) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(display_handle, 0, sizeof(display_handle_t));
    
    // Initialize SPI bus
    spi_bus_config_t bus_config = {
        .mosi_io_num = config->pin_mosi,
        .miso_io_num = -1,
        .sclk_io_num = config->pin_sclk,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * 2,
    };
    
    esp_err_t ret = spi_bus_initialize(SPI2_HOST, &bus_config, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Add SPI device
    spi_device_interface_config_t dev_config = {
        .clock_speed_hz = config->pixel_clock_hz,
        .mode = 0,
        .spics_io_num = config->pin_cs,
        .queue_size = 7,
        .pre_cb = lcd_spi_pre_transfer_callback,
    };
    
    ret = spi_bus_add_device(SPI2_HOST, &dev_config, &spi_device);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add SPI device: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Configure GPIO pins
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << config->pin_dc) | (1ULL << config->pin_rst),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = false,
        .pull_down_en = false,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    
    // Initialize backlight
    ret = backlight_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize backlight: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Initialize LCD
    ret = lcd_init_sequence();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize LCD: %s", esp_err_to_name(ret));
        return ret;
    }
    
    display_handle->width = DISPLAY_WIDTH;
    display_handle->height = DISPLAY_HEIGHT;
    display_handle->backlight_pin = config->pin_bl;
    display_handle->backlight_on_level = config->bl_on_level;
    display_handle->initialized = true;
    display_ready = true;
    
    ESP_LOGI(TAG, "ST7789 display initialized successfully (%dx%d)", 
             display_handle->width, display_handle->height);
    
    return ESP_OK;
}

esp_err_t display_deinit(display_handle_t *display_handle)
{
    if (!display_handle || !display_handle->initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (spi_device) {
        spi_bus_remove_device(spi_device);
        spi_device = NULL;
    }
    
    spi_bus_free(SPI2_HOST);
    display_ready = false;
    display_handle->initialized = false;
    
    return ESP_OK;
}

esp_err_t display_backlight_set(display_handle_t *display_handle, bool on)
{
    if (!display_handle || !display_handle->initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    return set_backlight(on ? 100 : 0);
}

esp_err_t display_clear(display_handle_t *display_handle, uint16_t color)
{
    if (!display_handle || !display_handle->initialized || !display_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    
    lcd_set_cursor(0, 0, DISPLAY_WIDTH - 1, DISPLAY_HEIGHT - 1);
    
    // Fill display with color
    for (int i = 0; i < DISPLAY_WIDTH * DISPLAY_HEIGHT; i++) {
        lcd_write_data_word(color);
    }
    
    return ESP_OK;
}

esp_err_t display_fill_rect(display_handle_t *display_handle, int x, int y, int width, int height, uint16_t color)
{
    if (!display_handle || !display_handle->initialized || !display_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (x < 0 || y < 0 || x + width > DISPLAY_WIDTH || y + height > DISPLAY_HEIGHT) {
        return ESP_ERR_INVALID_ARG;
    }
    
    lcd_set_cursor(x, y, x + width - 1, y + height - 1);
    
    for (int i = 0; i < width * height; i++) {
        lcd_write_data_word(color);
    }
    
    return ESP_OK;
}

esp_err_t display_draw_pixel(display_handle_t *display_handle, int x, int y, uint16_t color)
{
    if (!display_handle || !display_handle->initialized || !display_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (x < 0 || x >= DISPLAY_WIDTH || y < 0 || y >= DISPLAY_HEIGHT) {
        return ESP_ERR_INVALID_ARG;
    }
    
    lcd_set_cursor(x, y, x, y);
    lcd_write_data_word(color);
    
    return ESP_OK;
}

esp_err_t display_draw_char(display_handle_t *display_handle, int x, int y, char character, uint16_t fg_color, uint16_t bg_color)
{
    if (!display_handle || !display_handle->initialized || !display_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (x < 0 || y < 0 || x + FONT_WIDTH > DISPLAY_WIDTH || y + FONT_HEIGHT > DISPLAY_HEIGHT) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Draw solid blocks for all characters except space
    lcd_set_cursor(x, y, x + FONT_WIDTH - 1, y + FONT_HEIGHT - 1);
    
    uint16_t color = (character == ' ') ? bg_color : fg_color;
    
    for (int i = 0; i < FONT_WIDTH * FONT_HEIGHT; i++) {
        lcd_write_data_word(color);
    }
    
    return ESP_OK;
}

esp_err_t display_draw_string(display_handle_t *display_handle, int x, int y, const char *str, uint16_t fg_color, uint16_t bg_color)
{
    if (!display_handle || !display_handle->initialized || !display_ready || !str) {
        return ESP_ERR_INVALID_STATE;
    }
    
    int current_x = x;
    int current_y = y;
    
    while (*str && current_x < DISPLAY_WIDTH) {
        if (*str == '\n') {
            current_x = x;
            current_y += FONT_HEIGHT;
            if (current_y >= DISPLAY_HEIGHT) break;
        } else {
            esp_err_t ret = display_draw_char(display_handle, current_x, current_y, *str, fg_color, bg_color);
            if (ret != ESP_OK) return ret;
            current_x += FONT_WIDTH;
        }
        str++;
    }
    
    return ESP_OK;
}

esp_err_t display_printf(display_handle_t *display_handle, int x, int y, uint16_t fg_color, uint16_t bg_color, const char *format, ...)
{
    if (!display_handle || !display_handle->initialized || !display_ready || !format) {
        return ESP_ERR_INVALID_STATE;
    }
    
    char buffer[256];
    va_list args;
    va_start(args, format);
    int len = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    if (len < 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    return display_draw_string(display_handle, x, y, buffer, fg_color, bg_color);
}

esp_err_t lcd_add_window(uint16_t x_start, uint16_t y_start, uint16_t x_end, uint16_t y_end, uint16_t* color)
{
    if (!display_ready || !color) {
        return ESP_ERR_INVALID_STATE;
    }
    
    uint16_t width = x_end - x_start + 1;
    uint16_t height = y_end - y_start + 1;
    uint32_t num_pixels = width * height;
    
    lcd_set_cursor(x_start, y_start, x_end, y_end);
    
    // Send color data as bytes for better performance
    esp_err_t ret = lcd_write_data_nbytes((uint8_t*)color, num_pixels * 2);
    
    return ret;
}