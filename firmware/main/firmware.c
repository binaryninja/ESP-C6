/**
 * @file firmware.c
 * @brief ESP32-C6 Basic Firmware Example with Display
 *
 * A simple working firmware that demonstrates basic ESP32-C6 functionality
 * including ST7789 display support showing "Hello World"
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "display_st7789.h"
#include "lvgl_driver.h"

static const char *TAG = "ESP32-C6-FIRMWARE";

// GPIO configuration
#define STATUS_LED_GPIO         GPIO_NUM_8
#define USER_BUTTON_GPIO        GPIO_NUM_9

// Task priorities
#define STATUS_LED_TASK_PRIORITY    2
#define SYSTEM_MONITOR_TASK_PRIORITY 3
#define DISPLAY_TASK_PRIORITY       4

// Display handle
static display_handle_t s_display_handle = {0};
static bool s_display_initialized = false;

// System statistics
typedef struct {
    uint32_t uptime_seconds;
    uint32_t free_heap;
    uint32_t min_free_heap;
    uint32_t button_presses;
} system_stats_t;

static system_stats_t s_stats = {0};

/**
 * @brief Print startup banner with chip information
 */
static void print_startup_banner(void)
{
    printf("\n\n");
    printf("========================================\n");
    printf("     ESP32-C6 Comprehensive Firmware   \n");
    printf("========================================\n");

    esp_chip_info_t chip_info;
    uint32_t flash_size;

    esp_chip_info(&chip_info);
    esp_flash_get_size(NULL, &flash_size);

    ESP_LOGI(TAG, "Chip model: %s, revision %d",
             (chip_info.model == CHIP_ESP32C6) ? "ESP32-C6" : "Unknown",
             chip_info.revision);
    ESP_LOGI(TAG, "Number of cores: %d", chip_info.cores);
    ESP_LOGI(TAG, "Flash size: %"PRIu32"MB", flash_size / (1024 * 1024));
    ESP_LOGI(TAG, "Features: %s%s%s%s",
             (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "WiFi/" : "",
             (chip_info.features & CHIP_FEATURE_BLE) ? "BLE/" : "",
             (chip_info.features & CHIP_FEATURE_IEEE802154) ? "802.15.4/" : "",
             (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "Embedded-Flash" : "External-Flash");

    ESP_LOGI(TAG, "Free heap: %"PRIu32" bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "IDF version: %s", esp_get_idf_version());

    printf("========================================\n\n");
}

/**
 * @brief Initialize GPIO pins
 */
static void init_gpio(void)
{
    // Configure status LED
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << STATUS_LED_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(STATUS_LED_GPIO, 0);

    // Configure user button with pull-up
    io_conf.pin_bit_mask = 1ULL << USER_BUTTON_GPIO;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io_conf);

    ESP_LOGI(TAG, "GPIO initialized - LED: GPIO%d, Button: GPIO%d",
             STATUS_LED_GPIO, USER_BUTTON_GPIO);
}

/**
 * @brief Initialize the display with LVGL
 */
static void init_display(void)
{
    display_config_t display_config;
    display_get_default_config(&display_config);

    ESP_LOGI(TAG, "Initializing ST7789 display...");
    esp_err_t ret = display_init(&display_config, &s_display_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Display initialization failed: %s", esp_err_to_name(ret));
        s_display_initialized = false;
        return;
    }

    s_display_initialized = true;
    ESP_LOGI(TAG, "ST7789 display initialized successfully");

    // Initialize LVGL
    ESP_LOGI(TAG, "Initializing LVGL...");
    lvgl_init();

    ESP_LOGI(TAG, "LVGL initialized successfully");
}

/**
 * @brief Status LED task - blinks LED to show system is alive
 */
static void status_led_task(void* pvParameters)
{
    bool led_state = false;
    uint32_t blink_delay = 1000; // Default 1 second

    ESP_LOGI(TAG, "Status LED task started");

    // Add this task to the watchdog
    esp_task_wdt_add(NULL);

    while (1) {
        // Toggle LED
        led_state = !led_state;
        gpio_set_level(STATUS_LED_GPIO, led_state);

        // Adjust blink rate based on system health
        if (s_stats.free_heap < 20000) {
            blink_delay = 200; // Fast blink for low memory
        } else if (s_stats.uptime_seconds < 60) {
            blink_delay = 500; // Medium blink during startup
        } else {
            blink_delay = 1000; // Normal operation
        }

        // Reset watchdog for this task
        esp_task_wdt_reset();

        vTaskDelay(pdMS_TO_TICKS(blink_delay));
    }
}

/**
 * @brief System monitoring task
 */
static void system_monitor_task(void* pvParameters)
{
    static int last_button_state = 1;
    int current_button_state;

    ESP_LOGI(TAG, "System monitor task started");

    // Add this task to the watchdog
    esp_task_wdt_add(NULL);

    while (1) {
        // Update system statistics
        s_stats.uptime_seconds++;
        s_stats.free_heap = esp_get_free_heap_size();
        s_stats.min_free_heap = esp_get_minimum_free_heap_size();

        // Check button state
        current_button_state = gpio_get_level(USER_BUTTON_GPIO);
        if (last_button_state == 1 && current_button_state == 0) {
            // Button pressed (falling edge)
            s_stats.button_presses++;
            ESP_LOGI(TAG, "Button pressed! Count: %"PRIu32, s_stats.button_presses);

            // Print system status on button press
            ESP_LOGI(TAG, "=== System Status ===");
            ESP_LOGI(TAG, "Uptime: %"PRIu32" seconds", s_stats.uptime_seconds);
            ESP_LOGI(TAG, "Free heap: %"PRIu32" bytes (min: %"PRIu32")",
                     s_stats.free_heap, s_stats.min_free_heap);
            ESP_LOGI(TAG, "Button presses: %"PRIu32, s_stats.button_presses);
            ESP_LOGI(TAG, "==================");

            // Update display with color indication instead of text
            if (s_display_initialized) {
                display_clear(&s_display_handle, COLOR_BLACK);

                // Show different colors based on system state
                uint16_t status_color = COLOR_GREEN;
                if (s_stats.free_heap < 50000) status_color = COLOR_RED;
                if (s_stats.free_heap < 20000) status_color = COLOR_RED;

                // Fill screen with status color
                display_fill_rect(&s_display_handle, 0, 0, 320, 172, status_color);
                vTaskDelay(pdMS_TO_TICKS(1000));

                // Show button press count as colored bars
                int bar_width = (s_stats.button_presses % 10) * 32;
                display_fill_rect(&s_display_handle, 0, 0, bar_width, 20, COLOR_WHITE);
            }
        }
        last_button_state = current_button_state;

        // Log periodic status every 60 seconds
        if (s_stats.uptime_seconds % 60 == 0) {
            ESP_LOGI(TAG, "Uptime: %"PRIu32" minutes, Free heap: %"PRIu32" bytes",
                     s_stats.uptime_seconds / 60, s_stats.free_heap);
        }

        // Check for low memory condition
        if (s_stats.free_heap < 10000) {
            ESP_LOGW(TAG, "Low memory warning: %"PRIu32" bytes free", s_stats.free_heap);
        }

        // Check for button factory reset (hold for 5 seconds)
        static uint32_t button_hold_count = 0;
        if (current_button_state == 0) {
            button_hold_count++;
            if (button_hold_count >= 5) {
                ESP_LOGW(TAG, "Factory reset triggered by button hold");
                ESP_LOGW(TAG, "Erasing NVS and restarting...");
                nvs_flash_erase();
                esp_restart();
            }
        } else {
            button_hold_count = 0;
        }

        // Reset watchdog for this task
        esp_task_wdt_reset();

        vTaskDelay(pdMS_TO_TICKS(1000)); // Run every second
    }
}

/**
 * @brief Display task - updates display periodically
 */
static void display_task(void* pvParameters)
{
    if (!s_display_initialized) {
        ESP_LOGE(TAG, "Display task started but display not initialized");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Display task started");

    // Add this task to the watchdog
    esp_task_wdt_add(NULL);

    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(10); // 10ms interval

    while (1) {
        // Handle LVGL timer tasks
        lvgl_timer_loop();

        // Reset watchdog for this task
        esp_task_wdt_reset();

        // Use vTaskDelayUntil for more precise timing and better yielding
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

/**
 * @brief Initialize NVS flash
 */
static void init_nvs(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS flash was corrupted, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS flash initialized");
}

/**
 * @brief Application main entry point
 */
void app_main(void)
{
    // Print startup information
    print_startup_banner();

    // Initialize NVS
    init_nvs();

    // Initialize GPIO
    init_gpio();

    // Initialize display
    init_display();

    // Create and start tasks
    ESP_LOGI(TAG, "Starting application tasks...");

    // Create status LED task
    BaseType_t ret = xTaskCreate(
        status_led_task,
        "status_led",
        2048,
        NULL,
        STATUS_LED_TASK_PRIORITY,
        NULL
    );
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create status LED task");
        return;
    }

    // Create system monitor task
    ret = xTaskCreate(
        system_monitor_task,
        "sys_monitor",
        4096,
        NULL,
        SYSTEM_MONITOR_TASK_PRIORITY,
        NULL
    );
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create system monitor task");
        return;
    }

    // Create display task if display is initialized
    if (s_display_initialized) {
        ret = xTaskCreate(
            display_task,
            "display",
            4096,
            NULL,
            DISPLAY_TASK_PRIORITY,
            NULL
        );
        if (ret != pdPASS) {
            ESP_LOGE(TAG, "Failed to create display task");
            return;
        }
    }

    ESP_LOGI(TAG, "ESP32-C6 firmware started successfully!");
    ESP_LOGI(TAG, "Press the user button (GPIO%d) to display system status", USER_BUTTON_GPIO);
    ESP_LOGI(TAG, "Hold the button for 5 seconds to perform factory reset");
    ESP_LOGI(TAG, "Status LED (GPIO%d) indicates system health:", STATUS_LED_GPIO);
    ESP_LOGI(TAG, "  - 1s blink: Normal operation");
    ESP_LOGI(TAG, "  - 0.5s blink: Startup phase");
    ESP_LOGI(TAG, "  - 0.2s blink: Low memory warning");
    if (s_display_initialized) {
        ESP_LOGI(TAG, "ST7789 Display: Showing 'Hello World' and live system stats");
        ESP_LOGI(TAG, "Display updates every 10 seconds, button press refreshes stats");
    } else {
        ESP_LOGW(TAG, "Display initialization failed - running without display");
    }

    // Main task can exit, FreeRTOS will continue running the created tasks
}
