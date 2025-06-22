/**
 * @file firmware.c
 * @brief ESP32-C6 Basic Firmware Example
 * 
 * A simple working firmware that demonstrates basic ESP32-C6 functionality
 * and can be extended with the comprehensive tutorial features.
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
#include "nvs_flash.h"
#include "driver/gpio.h"

static const char *TAG = "ESP32-C6-FIRMWARE";

// GPIO configuration
#define STATUS_LED_GPIO         GPIO_NUM_8
#define USER_BUTTON_GPIO        GPIO_NUM_9

// Task priorities
#define STATUS_LED_TASK_PRIORITY    2
#define SYSTEM_MONITOR_TASK_PRIORITY 3

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
 * @brief Status LED task - blinks LED to show system is alive
 */
static void status_led_task(void* pvParameters)
{
    bool led_state = false;
    uint32_t blink_delay = 1000; // Default 1 second
    
    ESP_LOGI(TAG, "Status LED task started");
    
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
        
        vTaskDelay(pdMS_TO_TICKS(1000)); // Run every second
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
    
    ESP_LOGI(TAG, "ESP32-C6 firmware started successfully!");
    ESP_LOGI(TAG, "Press the user button (GPIO%d) to display system status", USER_BUTTON_GPIO);
    ESP_LOGI(TAG, "Hold the button for 5 seconds to perform factory reset");
    ESP_LOGI(TAG, "Status LED (GPIO%d) indicates system health:", STATUS_LED_GPIO);
    ESP_LOGI(TAG, "  - 1s blink: Normal operation");
    ESP_LOGI(TAG, "  - 0.5s blink: Startup phase");
    ESP_LOGI(TAG, "  - 0.2s blink: Low memory warning");
    
    // Main task can exit, FreeRTOS will continue running the created tasks
}