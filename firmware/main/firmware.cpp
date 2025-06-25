/**
 * @file firmware.cpp
 * @brief ESP32-C6 Comprehensive Firmware with TinyMCP Integration
 *
 * Advanced firmware demonstrating ESP32-C6 functionality including:
 * - ST7789 display support with "Hello World" 
 * - TinyMCP (Model Context Protocol) server
 * - Remote control via JSON-RPC commands
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
#include "lvgl.h"

extern "C" {
#include "mcp_server_simple.h"
#include "wifi_manager.h"
}

static const char *TAG = "firmware";

// GPIO Configuration
#define STATUS_LED_GPIO         GPIO_NUM_8
#define USER_BUTTON_GPIO        GPIO_NUM_9

// Task priorities
#define STATUS_LED_TASK_PRIORITY    2
#define SYSTEM_MONITOR_TASK_PRIORITY 3
#define DISPLAY_TASK_PRIORITY       4
#define MCP_SERVER_TASK_PRIORITY    5

// Display handle
static display_handle_t s_display_handle = {0};
static bool s_display_initialized = false;

// LVGL objects
static lv_obj_t *stats_label = NULL;
static lv_obj_t *uptime_label = NULL;
static lv_obj_t *heap_label = NULL;
static lv_obj_t *button_label = NULL;
static lv_obj_t *wifi_label = NULL;

// System statistics with Wi-Fi info
typedef struct {
    uint32_t uptime_seconds;
    uint32_t free_heap;
    uint32_t min_free_heap;
    uint32_t button_presses;
    char wifi_ssid[33];
    char wifi_ip[16];
    int8_t wifi_rssi;
    bool wifi_connected;
} system_stats_t;

static system_stats_t s_stats = {0};

// MCP Server handle
static mcp_server_handle_t s_mcp_server = NULL;
static bool s_mcp_server_initialized = false;

// Wi-Fi Manager
static bool s_wifi_initialized = false;

// Function declarations
static void create_stats_display(void);
static void update_stats_display(void);
static void init_mcp_server(void);
static void init_wifi(void);
static void wifi_event_callback(wifi_status_t status, uint32_t ip_addr);

// C linkage functions for MCP tools
extern "C" {
    void* get_display_handle(void) {
        return s_display_initialized ? (void*)&s_display_handle : NULL;
    }
    
    uint32_t get_button_press_count(void) {
        return s_stats.button_presses;
    }
}

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

    // Create LVGL objects for system stats display
    create_stats_display();

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
            ESP_LOGI(TAG, "Wi-Fi: %s (%s) RSSI: %ddBm", 
                     s_stats.wifi_ssid, s_stats.wifi_ip, s_stats.wifi_rssi);
            ESP_LOGI(TAG, "==================");

            // Update display with system stats
            if (s_display_initialized) {
                update_stats_display();
            }
        }
        last_button_state = current_button_state;

        // Log periodic status every 60 seconds
        if (s_stats.uptime_seconds % 60 == 0) {
            ESP_LOGI(TAG, "Uptime: %"PRIu32" minutes, Free heap: %"PRIu32" bytes, Wi-Fi: %s",
                     s_stats.uptime_seconds / 60, s_stats.free_heap, 
                     s_stats.wifi_connected ? "Connected" : "Disconnected");
        }

        // Update Wi-Fi RSSI periodically if connected
        if (s_stats.wifi_connected && s_stats.uptime_seconds % 30 == 0) {
            wifi_stats_t wifi_stats;
            if (wifi_manager_get_stats(&wifi_stats) == ESP_OK) {
                s_stats.wifi_rssi = wifi_stats.rssi;
            }
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
 * @brief Create LVGL objects for system stats display
 */
static void create_stats_display(void)
{
    if (!s_display_initialized) {
        return;
    }

    // Create main title label
    stats_label = lv_label_create(lv_scr_act());
    lv_label_set_text(stats_label, "ESP32-C6 System Stats");
    lv_obj_align(stats_label, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_style_text_color(stats_label, lv_color_white(), 0);

    // Create uptime label
    uptime_label = lv_label_create(lv_scr_act());
    lv_label_set_text(uptime_label, "Uptime: 0s");
    lv_obj_align(uptime_label, LV_ALIGN_TOP_LEFT, 10, 40);
    lv_obj_set_style_text_color(uptime_label, lv_color_white(), 0);

    // Create heap label
    heap_label = lv_label_create(lv_scr_act());
    lv_label_set_text(heap_label, "Free Heap: 0 bytes");
    lv_obj_align(heap_label, LV_ALIGN_TOP_LEFT, 10, 70);
    lv_obj_set_style_text_color(heap_label, lv_color_white(), 0);

    // Create button press label
    button_label = lv_label_create(lv_scr_act());
    lv_label_set_text(button_label, "Button Presses: 0");
    lv_obj_align(button_label, LV_ALIGN_TOP_LEFT, 10, 100);
    lv_obj_set_style_text_color(button_label, lv_color_white(), 0);

    // Create Wi-Fi status label
    wifi_label = lv_label_create(lv_scr_act());
    lv_label_set_text(wifi_label, "Wi-Fi: Not connected");
    lv_obj_align(wifi_label, LV_ALIGN_TOP_LEFT, 10, 130);
    lv_obj_set_style_text_color(wifi_label, lv_color_white(), 0);

    ESP_LOGI(TAG, "Stats display created with Wi-Fi status");
}

/**
 * @brief Update system stats display
 */
static void update_stats_display(void)
{
    if (!s_display_initialized || !stats_label) {
        return;
    }

    static char uptime_str[32];
    static char heap_str[64];
    static char button_str[32];
    static char wifi_str[80];

    // Format uptime
    uint32_t hours = s_stats.uptime_seconds / 3600;
    uint32_t minutes = (s_stats.uptime_seconds % 3600) / 60;
    uint32_t seconds = s_stats.uptime_seconds % 60;
    
    if (hours > 0) {
        snprintf(uptime_str, sizeof(uptime_str), "Uptime: %"PRIu32"h %"PRIu32"m %"PRIu32"s", hours, minutes, seconds);
    } else if (minutes > 0) {
        snprintf(uptime_str, sizeof(uptime_str), "Uptime: %"PRIu32"m %"PRIu32"s", minutes, seconds);
    } else {
        snprintf(uptime_str, sizeof(uptime_str), "Uptime: %"PRIu32"s", seconds);
    }

    // Format heap info
    snprintf(heap_str, sizeof(heap_str), "Free Heap: %"PRIu32" bytes\nMin Heap: %"PRIu32" bytes", 
             s_stats.free_heap, s_stats.min_free_heap);

    // Format button presses
    snprintf(button_str, sizeof(button_str), "Button Presses: %"PRIu32, s_stats.button_presses);

    // Format Wi-Fi status
    if (s_stats.wifi_connected) {
        snprintf(wifi_str, sizeof(wifi_str), "Wi-Fi: %s\nIP: %s (RSSI: %ddBm)", 
                 s_stats.wifi_ssid, s_stats.wifi_ip, s_stats.wifi_rssi);
    } else {
        snprintf(wifi_str, sizeof(wifi_str), "Wi-Fi: %s", s_stats.wifi_ssid);
    }

    // Update labels
    lv_label_set_text(uptime_label, uptime_str);
    lv_label_set_text(heap_label, heap_str);
    lv_label_set_text(button_label, button_str);
    lv_label_set_text(wifi_label, wifi_str);

    // Change colors based on system health
    lv_color_t heap_color = lv_color_white();
    if (s_stats.free_heap < 20000) {
        heap_color = lv_color_make(255, 0, 0); // Red for critical
    } else if (s_stats.free_heap < 50000) {
        heap_color = lv_color_make(255, 255, 0); // Yellow for warning
    } else {
        heap_color = lv_color_make(0, 255, 0); // Green for healthy
    }
    lv_obj_set_style_text_color(heap_label, heap_color, 0);

    // Set Wi-Fi label color based on connection status
    lv_color_t wifi_color = s_stats.wifi_connected ? 
        lv_color_make(0, 255, 0) : lv_color_make(255, 255, 0);
    lv_obj_set_style_text_color(wifi_label, wifi_color, 0);
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
    
    uint32_t update_counter = 0;

    while (1) {
        // Handle LVGL timer tasks
        lvgl_timer_loop();

        // Update display stats every 100 cycles (approximately 1 second)
        if (++update_counter >= 100) {
            update_stats_display();
            update_counter = 0;
        }

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
 * @brief Initialize MCP server
 */
static void init_mcp_server(void)
{
    ESP_LOGI(TAG, "Initializing simple MCP server...");
    
    // Get default configuration
    mcp_server_config_t config;
    esp_err_t ret = mcp_server_get_default_config(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get MCP server default config: %s", esp_err_to_name(ret));
        return;
    }
    
    // Configure server
    config.task_priority = MCP_SERVER_TASK_PRIORITY;
    config.enable_echo_tool = true;
    config.enable_display_tool = true;
    config.enable_gpio_tool = true;
    config.enable_system_tool = true;
    
    // Initialize server
    ret = mcp_server_init(&config, &s_mcp_server);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize MCP server: %s", esp_err_to_name(ret));
        return;
    }
    
    // Start server
    ret = mcp_server_start(s_mcp_server);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start MCP server: %s", esp_err_to_name(ret));
        mcp_server_deinit(s_mcp_server);
        s_mcp_server = NULL;
        return;
    }
    
    s_mcp_server_initialized = true;
    ESP_LOGI(TAG, "Simple MCP server initialized and started successfully");
    ESP_LOGI(TAG, "MCP Tools available:");
    ESP_LOGI(TAG, "  - echo: Echo back input parameters");
    ESP_LOGI(TAG, "  - display_control: Control ST7789 display");
    ESP_LOGI(TAG, "  - gpio_control: Control LED and read button");
    ESP_LOGI(TAG, "  - system_info: Get system information");
}

/**
 * @brief Wi-Fi event callback
 */
static void wifi_event_callback(wifi_status_t status, uint32_t ip_addr)
{
    switch (status) {
        case WIFI_STATUS_CONNECTING:
            ESP_LOGI(TAG, "Wi-Fi: Connecting...");
            strcpy(s_stats.wifi_ssid, "Connecting...");
            strcpy(s_stats.wifi_ip, "0.0.0.0");
            s_stats.wifi_connected = false;
            s_stats.wifi_rssi = 0;
            break;
            
        case WIFI_STATUS_CONNECTED:
            ESP_LOGI(TAG, "Wi-Fi: Connected successfully");
            wifi_manager_get_config_info(s_stats.wifi_ssid, sizeof(s_stats.wifi_ssid), NULL, NULL);
            wifi_manager_get_ip_string(s_stats.wifi_ip, sizeof(s_stats.wifi_ip));
            s_stats.wifi_connected = true;
            
            // Get RSSI
            wifi_stats_t wifi_stats;
            if (wifi_manager_get_stats(&wifi_stats) == ESP_OK) {
                s_stats.wifi_rssi = wifi_stats.rssi;
            }
            break;
            
        case WIFI_STATUS_DISCONNECTED:
            ESP_LOGW(TAG, "Wi-Fi: Disconnected");
            strcpy(s_stats.wifi_ssid, "Disconnected");
            strcpy(s_stats.wifi_ip, "0.0.0.0");
            s_stats.wifi_connected = false;
            s_stats.wifi_rssi = 0;
            break;
            
        case WIFI_STATUS_FAILED:
            ESP_LOGE(TAG, "Wi-Fi: Connection failed");
            strcpy(s_stats.wifi_ssid, "Failed");
            strcpy(s_stats.wifi_ip, "0.0.0.0");
            s_stats.wifi_connected = false;
            s_stats.wifi_rssi = 0;
            break;
            
        case WIFI_STATUS_RECONNECTING:
            ESP_LOGI(TAG, "Wi-Fi: Reconnecting...");
            strcpy(s_stats.wifi_ssid, "Reconnecting...");
            strcpy(s_stats.wifi_ip, "0.0.0.0");
            s_stats.wifi_connected = false;
            s_stats.wifi_rssi = 0;
            break;
    }
}

/**
 * @brief Initialize Wi-Fi Manager
 */
static void init_wifi(void)
{
    ESP_LOGI(TAG, "Initializing Wi-Fi manager...");
    
    // Initialize default Wi-Fi stats
    strcpy(s_stats.wifi_ssid, "Not connected");
    strcpy(s_stats.wifi_ip, "0.0.0.0");
    s_stats.wifi_connected = false;
    s_stats.wifi_rssi = 0;
    
    // Configure Wi-Fi manager
    wifi_manager_config_t config = WIFI_MANAGER_CONFIG_DEFAULT();
    config.max_retry_attempts = 15;
    config.retry_delay_ms = 3000;
    config.auto_reconnect = true;
    config.power_save_mode = WIFI_PS_MIN_MODEM;
    
    // Initialize Wi-Fi manager
    esp_err_t ret = wifi_manager_init(&config, wifi_event_callback);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize Wi-Fi manager: %s", esp_err_to_name(ret));
        return;
    }
    
    // Start Wi-Fi manager
    ret = wifi_manager_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start Wi-Fi manager: %s", esp_err_to_name(ret));
        return;
    }
    
    s_wifi_initialized = true;
    ESP_LOGI(TAG, "Wi-Fi manager initialized and started successfully");
}

/**
 * @brief Application main entry point
 */
extern "C" void app_main(void)
{
    // Print startup information
    print_startup_banner();

    // Initialize NVS
    init_nvs();

    // Initialize GPIO
    init_gpio();

    // Initialize display
    init_display();

    // Initialize Wi-Fi
    init_wifi();

    // Initialize MCP server
    init_mcp_server();

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

    ESP_LOGI(TAG, "ESP32-C6 firmware with Wi-Fi and TinyMCP started successfully!");
    ESP_LOGI(TAG, "Press the user button (GPIO%d) to display system status", USER_BUTTON_GPIO);
    ESP_LOGI(TAG, "Hold the button for 5 seconds to perform factory reset");
    ESP_LOGI(TAG, "Status LED (GPIO%d) indicates system health:", STATUS_LED_GPIO);
    ESP_LOGI(TAG, "  - 1s blink: Normal operation");
    ESP_LOGI(TAG, "  - 0.5s blink: Startup phase");
    ESP_LOGI(TAG, "  - 0.2s blink: Low memory warning");
    if (s_display_initialized) {
        ESP_LOGI(TAG, "ST7789 Display: Showing live system stats including Wi-Fi");
        ESP_LOGI(TAG, "Display updates every second, button press refreshes stats");
    } else {
        ESP_LOGW(TAG, "Display initialization failed - running without display");
    }
    if (s_wifi_initialized) {
        ESP_LOGI(TAG, "Wi-Fi Manager: Connecting to FBI Surveillance Van");
        ESP_LOGI(TAG, "Wi-Fi status will be shown on display and in logs");
    } else {
        ESP_LOGW(TAG, "Wi-Fi initialization failed - running without Wi-Fi");
    }
    if (s_mcp_server_initialized) {
        ESP_LOGI(TAG, "Simple MCP Server: Ready for JSON-RPC commands");
        ESP_LOGI(TAG, "Send JSON-RPC requests to control display, GPIO, and get system info");
        ESP_LOGI(TAG, "Example: {\"jsonrpc\":\"2.0\",\"method\":\"tools/list\",\"id\":1}");
    } else {
        ESP_LOGW(TAG, "MCP server initialization failed - running without MCP support");
    }

    // Main task can exit, FreeRTOS will continue running the created tasks
}
