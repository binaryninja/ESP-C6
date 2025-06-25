/**
 * @file mcp_tools.h
 * @brief ESP32-C6 MCP Tools Interface
 * 
 * This header defines the MCP tools available on ESP32-C6, including
 * display control, GPIO management, system monitoring, and status reporting.
 * 
 * Features:
 * - DisplayTool: Control ST7789 display and LVGL widgets
 * - GPIOTool: Manage LED, button, and general GPIO operations
 * - SystemTool: Provide system information and statistics
 * - StatusTool: Report device health and operational status
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/* Tool Name Definitions */
#define MCP_TOOL_DISPLAY_NAME           "display_control"
#define MCP_TOOL_GPIO_NAME              "gpio_control"
#define MCP_TOOL_SYSTEM_NAME            "system_info"
#define MCP_TOOL_STATUS_NAME            "device_status"

/* Tool Descriptions */
#define MCP_TOOL_DISPLAY_DESCRIPTION    "Control ST7789 display and LVGL widgets"
#define MCP_TOOL_GPIO_DESCRIPTION       "Control GPIO pins and read hardware state"
#define MCP_TOOL_SYSTEM_DESCRIPTION     "Get system information and statistics"
#define MCP_TOOL_STATUS_DESCRIPTION     "Get device health and operational status"

/* Display Tool Actions */
typedef enum {
    MCP_DISPLAY_ACTION_SHOW_TEXT = 0,
    MCP_DISPLAY_ACTION_CLEAR,
    MCP_DISPLAY_ACTION_SET_BRIGHTNESS,
    MCP_DISPLAY_ACTION_DRAW_RECT,
    MCP_DISPLAY_ACTION_DRAW_PIXEL,
    MCP_DISPLAY_ACTION_GET_INFO,
    MCP_DISPLAY_ACTION_REFRESH,
    MCP_DISPLAY_ACTION_MAX
} mcp_display_action_t;

/* GPIO Tool Actions */
typedef enum {
    MCP_GPIO_ACTION_SET_LED = 0,
    MCP_GPIO_ACTION_READ_BUTTON,
    MCP_GPIO_ACTION_GET_STATUS,
    MCP_GPIO_ACTION_SET_PIN,
    MCP_GPIO_ACTION_READ_PIN,
    MCP_GPIO_ACTION_CONFIG_PIN,
    MCP_GPIO_ACTION_MAX
} mcp_gpio_action_t;

/* System Tool Actions */
typedef enum {
    MCP_SYSTEM_ACTION_GET_INFO = 0,
    MCP_SYSTEM_ACTION_GET_STATS,
    MCP_SYSTEM_ACTION_GET_MEMORY,
    MCP_SYSTEM_ACTION_GET_TASKS,
    MCP_SYSTEM_ACTION_RESTART,
    MCP_SYSTEM_ACTION_FACTORY_RESET,
    MCP_SYSTEM_ACTION_MAX
} mcp_system_action_t;

/* Status Tool Actions */
typedef enum {
    MCP_STATUS_ACTION_GET_HEALTH = 0,
    MCP_STATUS_ACTION_GET_SENSORS,
    MCP_STATUS_ACTION_GET_CONNECTIONS,
    MCP_STATUS_ACTION_RUN_DIAGNOSTICS,
    MCP_STATUS_ACTION_MAX
} mcp_status_action_t;

/* Display Colors (RGB565) */
typedef enum {
    MCP_COLOR_BLACK = 0x0000,
    MCP_COLOR_WHITE = 0xFFFF,
    MCP_COLOR_RED = 0xF800,
    MCP_COLOR_GREEN = 0x07E0,
    MCP_COLOR_BLUE = 0x001F,
    MCP_COLOR_YELLOW = 0xFFE0,
    MCP_COLOR_CYAN = 0x07FF,
    MCP_COLOR_MAGENTA = 0xF81F
} mcp_display_color_t;

/* GPIO Pin Numbers */
typedef enum {
    MCP_GPIO_LED = 8,
    MCP_GPIO_BUTTON = 9,
    MCP_GPIO_DISPLAY_MOSI = 6,
    MCP_GPIO_DISPLAY_SCLK = 7,
    MCP_GPIO_DISPLAY_CS = 14,
    MCP_GPIO_DISPLAY_DC = 15,
    MCP_GPIO_DISPLAY_RST = 21,
    MCP_GPIO_DISPLAY_BL = 22
} mcp_gpio_pin_t;

/* Display Tool Parameters */
typedef struct {
    mcp_display_action_t action;
    const char* text;
    int x;
    int y;
    int width;
    int height;
    mcp_display_color_t color;
    mcp_display_color_t bg_color;
    int brightness;
} mcp_display_params_t;

/* GPIO Tool Parameters */
typedef struct {
    mcp_gpio_action_t action;
    mcp_gpio_pin_t pin;
    bool state;
    int mode;
    int pull_mode;
} mcp_gpio_params_t;

/* System Tool Parameters */
typedef struct {
    mcp_system_action_t action;
    bool include_tasks;
    bool include_memory;
    bool force_restart;
} mcp_system_params_t;

/* Status Tool Parameters */
typedef struct {
    mcp_status_action_t action;
    bool include_sensors;
    bool run_full_diagnostics;
} mcp_status_params_t;

/* Tool Result Structures */
typedef struct {
    bool success;
    const char* message;
    int display_width;
    int display_height;
    int brightness;
    bool backlight_on;
} mcp_display_result_t;

typedef struct {
    bool success;
    const char* message;
    bool pin_state;
    int pin_value;
    bool button_pressed;
    uint32_t button_count;
} mcp_gpio_result_t;

typedef struct {
    bool success;
    const char* message;
    const char* chip_model;
    const char* idf_version;
    uint32_t free_heap;
    uint32_t min_free_heap;
    uint64_t uptime_ms;
    uint32_t reset_reason;
    float cpu_freq_mhz;
} mcp_system_result_t;

typedef struct {
    bool success;
    const char* message;
    const char* health_status;
    float temperature;
    uint32_t error_count;
    bool display_ok;
    bool gpio_ok;
    bool memory_ok;
} mcp_status_result_t;

/* Tool Schema JSON Strings */
extern const char* MCP_TOOL_DISPLAY_SCHEMA;
extern const char* MCP_TOOL_GPIO_SCHEMA;
extern const char* MCP_TOOL_SYSTEM_SCHEMA;
extern const char* MCP_TOOL_STATUS_SCHEMA;

/**
 * @brief Parse display tool parameters from JSON
 * 
 * @param params_json JSON parameters string
 * @param params Parsed parameters structure
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mcp_tool_display_parse_params(const char* params_json, 
                                        mcp_display_params_t* params);

/**
 * @brief Parse GPIO tool parameters from JSON
 * 
 * @param params_json JSON parameters string
 * @param params Parsed parameters structure
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mcp_tool_gpio_parse_params(const char* params_json, 
                                     mcp_gpio_params_t* params);

/**
 * @brief Parse system tool parameters from JSON
 * 
 * @param params_json JSON parameters string
 * @param params Parsed parameters structure
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mcp_tool_system_parse_params(const char* params_json, 
                                       mcp_system_params_t* params);

/**
 * @brief Parse status tool parameters from JSON
 * 
 * @param params_json JSON parameters string
 * @param params Parsed parameters structure
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mcp_tool_status_parse_params(const char* params_json, 
                                       mcp_status_params_t* params);

/**
 * @brief Format display tool result to JSON
 * 
 * @param result Result structure
 * @param result_json Output JSON buffer
 * @param result_size Size of output buffer
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mcp_tool_display_format_result(const mcp_display_result_t* result,
                                         char* result_json, 
                                         size_t result_size);

/**
 * @brief Format GPIO tool result to JSON
 * 
 * @param result Result structure
 * @param result_json Output JSON buffer
 * @param result_size Size of output buffer
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mcp_tool_gpio_format_result(const mcp_gpio_result_t* result,
                                      char* result_json, 
                                      size_t result_size);

/**
 * @brief Format system tool result to JSON
 * 
 * @param result Result structure
 * @param result_json Output JSON buffer
 * @param result_size Size of output buffer
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mcp_tool_system_format_result(const mcp_system_result_t* result,
                                        char* result_json, 
                                        size_t result_size);

/**
 * @brief Format status tool result to JSON
 * 
 * @param result Result structure
 * @param result_json Output JSON buffer
 * @param result_size Size of output buffer
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mcp_tool_status_format_result(const mcp_status_result_t* result,
                                        char* result_json, 
                                        size_t result_size);

/**
 * @brief Validate display tool parameters
 * 
 * @param params Parameters to validate
 * @return ESP_OK if valid, error code otherwise
 */
esp_err_t mcp_tool_display_validate_params(const mcp_display_params_t* params);

/**
 * @brief Validate GPIO tool parameters
 * 
 * @param params Parameters to validate
 * @return ESP_OK if valid, error code otherwise
 */
esp_err_t mcp_tool_gpio_validate_params(const mcp_gpio_params_t* params);

/**
 * @brief Validate system tool parameters
 * 
 * @param params Parameters to validate
 * @return ESP_OK if valid, error code otherwise
 */
esp_err_t mcp_tool_system_validate_params(const mcp_system_params_t* params);

/**
 * @brief Validate status tool parameters
 * 
 * @param params Parameters to validate
 * @return ESP_OK if valid, error code otherwise
 */
esp_err_t mcp_tool_status_validate_params(const mcp_status_params_t* params);

/**
 * @brief Get display tool schema as JSON string
 * 
 * @return JSON schema string
 */
const char* mcp_tool_display_get_schema(void);

/**
 * @brief Get GPIO tool schema as JSON string
 * 
 * @return JSON schema string
 */
const char* mcp_tool_gpio_get_schema(void);

/**
 * @brief Get system tool schema as JSON string
 * 
 * @return JSON schema string
 */
const char* mcp_tool_system_get_schema(void);

/**
 * @brief Get status tool schema as JSON string
 * 
 * @return JSON schema string
 */
const char* mcp_tool_status_get_schema(void);

#ifdef __cplusplus
}
#endif