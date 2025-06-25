/**
 * @file gpio_tool.cpp
 * @brief MCP GPIO Tool Implementation for ESP32-C6
 * 
 * This file implements the GPIO control tool for MCP, allowing remote
 * control of LEDs, buttons, and general GPIO pins through JSON-RPC commands.
 */

#include "mcp_tools.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_err.h"
#include "driver/gpio.h"
#include "cJSON.h"

static const char *TAG = "MCP_GPIO_TOOL";

/* External button counter - defined in main firmware */
extern uint32_t get_button_press_count(void);

/* GPIO Tool JSON Schema */
const char* MCP_TOOL_GPIO_SCHEMA = 
"{"
  "\"type\": \"object\","
  "\"properties\": {"
    "\"action\": {"
      "\"type\": \"string\","
      "\"enum\": [\"set_led\", \"read_button\", \"get_status\", \"set_pin\", \"read_pin\", \"config_pin\"],"
      "\"description\": \"Action to perform on GPIO\""
    "},"
    "\"pin\": {"
      "\"type\": \"integer\","
      "\"minimum\": 0,"
      "\"maximum\": 30,"
      "\"description\": \"GPIO pin number\""
    "},"
    "\"state\": {"
      "\"type\": \"boolean\","
      "\"description\": \"Pin state (true=high, false=low)\""
    "},"
    "\"mode\": {"
      "\"type\": \"integer\","
      "\"enum\": [0, 1, 2, 3],"
      "\"description\": \"GPIO mode (0=input, 1=output, 2=input_pullup, 3=input_pulldown)\""
    "},"
    "\"pull_mode\": {"
      "\"type\": \"integer\","
      "\"enum\": [0, 1, 2],"
      "\"description\": \"Pull mode (0=floating, 1=pullup, 2=pulldown)\""
    "}"
  "},"
  "\"required\": [\"action\"]"
"}";

/* Helper function to convert action string to enum */
static mcp_gpio_action_t mcp_gpio_string_to_action(const char* action_str)
{
    if (!action_str) return MCP_GPIO_ACTION_GET_STATUS;
    
    if (strcmp(action_str, "set_led") == 0) return MCP_GPIO_ACTION_SET_LED;
    if (strcmp(action_str, "read_button") == 0) return MCP_GPIO_ACTION_READ_BUTTON;
    if (strcmp(action_str, "get_status") == 0) return MCP_GPIO_ACTION_GET_STATUS;
    if (strcmp(action_str, "set_pin") == 0) return MCP_GPIO_ACTION_SET_PIN;
    if (strcmp(action_str, "read_pin") == 0) return MCP_GPIO_ACTION_READ_PIN;
    if (strcmp(action_str, "config_pin") == 0) return MCP_GPIO_ACTION_CONFIG_PIN;
    
    return MCP_GPIO_ACTION_GET_STATUS; // Default
}

/* Helper function to convert mode integer to gpio_mode_t */
static gpio_mode_t mcp_gpio_int_to_mode(int mode)
{
    switch (mode) {
        case 0: return GPIO_MODE_INPUT;
        case 1: return GPIO_MODE_OUTPUT;
        case 2: return GPIO_MODE_INPUT;  // Will set pullup separately
        case 3: return GPIO_MODE_INPUT;  // Will set pulldown separately
        default: return GPIO_MODE_INPUT;
    }
}

/* Helper function to convert pull mode integer to gpio_pull_mode_t */
static gpio_pull_mode_t mcp_gpio_int_to_pull_mode(int pull_mode)
{
    switch (pull_mode) {
        case 0: return GPIO_FLOATING;
        case 1: return GPIO_PULLUP_ONLY;
        case 2: return GPIO_PULLDOWN_ONLY;
        default: return GPIO_FLOATING;
    }
}

/* Parse GPIO tool parameters from JSON */
esp_err_t mcp_tool_gpio_parse_params(const char* params_json, 
                                     mcp_gpio_params_t* params)
{
    if (!params_json || !params) {
        return ESP_ERR_INVALID_ARG;
    }
    
    cJSON* json = cJSON_Parse(params_json);
    if (!json) {
        ESP_LOGE(TAG, "Failed to parse parameters JSON");
        return ESP_ERR_INVALID_ARG;
    }
    
    /* Initialize default values */
    memset(params, 0, sizeof(mcp_gpio_params_t));
    params->pin = MCP_GPIO_LED; // Default to LED pin
    params->state = false;
    params->mode = 1; // Default to output
    params->pull_mode = 0; // Default to floating
    
    /* Parse action (required) */
    cJSON* action = cJSON_GetObjectItem(json, "action");
    if (!action || !cJSON_IsString(action)) {
        ESP_LOGE(TAG, "Missing or invalid action parameter");
        cJSON_Delete(json);
        return ESP_ERR_INVALID_ARG;
    }
    params->action = mcp_gpio_string_to_action(cJSON_GetStringValue(action));
    
    /* Parse optional parameters */
    cJSON* pin = cJSON_GetObjectItem(json, "pin");
    if (pin && cJSON_IsNumber(pin)) {
        params->pin = (mcp_gpio_pin_t)(int)cJSON_GetNumberValue(pin);
    }
    
    cJSON* state = cJSON_GetObjectItem(json, "state");
    if (state && cJSON_IsBool(state)) {
        params->state = cJSON_IsTrue(state);
    }
    
    cJSON* mode = cJSON_GetObjectItem(json, "mode");
    if (mode && cJSON_IsNumber(mode)) {
        params->mode = (int)cJSON_GetNumberValue(mode);
    }
    
    cJSON* pull_mode = cJSON_GetObjectItem(json, "pull_mode");
    if (pull_mode && cJSON_IsNumber(pull_mode)) {
        params->pull_mode = (int)cJSON_GetNumberValue(pull_mode);
    }
    
    cJSON_Delete(json);
    return ESP_OK;
}

/* Validate GPIO tool parameters */
esp_err_t mcp_tool_gpio_validate_params(const mcp_gpio_params_t* params)
{
    if (!params) {
        return ESP_ERR_INVALID_ARG;
    }
    
    /* Validate pin number */
    if (params->pin < 0 || params->pin > 30) {
        ESP_LOGE(TAG, "Invalid GPIO pin: %d", params->pin);
        return ESP_ERR_INVALID_ARG;
    }
    
    /* Check if pin is valid for ESP32-C6 */
    if (params->pin >= 25 && params->pin <= 30) {
        ESP_LOGE(TAG, "GPIO pin %d not available on ESP32-C6", params->pin);
        return ESP_ERR_INVALID_ARG;
    }
    
    /* Validate mode */
    if (params->mode < 0 || params->mode > 3) {
        ESP_LOGE(TAG, "Invalid GPIO mode: %d", params->mode);
        return ESP_ERR_INVALID_ARG;
    }
    
    /* Validate pull mode */
    if (params->pull_mode < 0 || params->pull_mode > 2) {
        ESP_LOGE(TAG, "Invalid pull mode: %d", params->pull_mode);
        return ESP_ERR_INVALID_ARG;
    }
    
    return ESP_OK;
}

/* Format GPIO tool result to JSON */
esp_err_t mcp_tool_gpio_format_result(const mcp_gpio_result_t* result,
                                      char* result_json, 
                                      size_t result_size)
{
    if (!result || !result_json || result_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    cJSON* json = cJSON_CreateObject();
    if (!json) {
        return ESP_ERR_NO_MEM;
    }
    
    cJSON_AddBoolToObject(json, "success", result->success);
    
    if (result->message) {
        cJSON_AddStringToObject(json, "message", result->message);
    }
    
    if (result->success) {
        cJSON_AddBoolToObject(json, "pin_state", result->pin_state);
        cJSON_AddNumberToObject(json, "pin_value", result->pin_value);
        cJSON_AddBoolToObject(json, "button_pressed", result->button_pressed);
        cJSON_AddNumberToObject(json, "button_count", result->button_count);
    }
    
    char* json_string = cJSON_Print(json);
    if (!json_string) {
        cJSON_Delete(json);
        return ESP_ERR_NO_MEM;
    }
    
    if (strlen(json_string) >= result_size) {
        free(json_string);
        cJSON_Delete(json);
        return ESP_ERR_INVALID_SIZE;
    }
    
    strcpy(result_json, json_string);
    free(json_string);
    cJSON_Delete(json);
    
    return ESP_OK;
}

/* Get GPIO tool schema as JSON string */
const char* mcp_tool_gpio_get_schema(void)
{
    return MCP_TOOL_GPIO_SCHEMA;
}

/* Execute GPIO tool */
esp_err_t mcp_tool_gpio_execute(const char* params_json, char* result_json, size_t result_size)
{
    if (!params_json || !result_json || result_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Executing GPIO tool: %s", params_json);
    
    /* Parse parameters */
    mcp_gpio_params_t params;
    esp_err_t ret = mcp_tool_gpio_parse_params(params_json, &params);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to parse parameters: %s", esp_err_to_name(ret));
        
        mcp_gpio_result_t error_result = {
            .success = false,
            .message = "Invalid parameters",
            .pin_state = false,
            .pin_value = 0,
            .button_pressed = false,
            .button_count = 0
        };
        
        return mcp_tool_gpio_format_result(&error_result, result_json, result_size);
    }
    
    /* Validate parameters */
    ret = mcp_tool_gpio_validate_params(&params);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Invalid parameters: %s", esp_err_to_name(ret));
        
        mcp_gpio_result_t error_result = {
            .success = false,
            .message = "Parameter validation failed",
            .pin_state = false,
            .pin_value = 0,
            .button_pressed = false,
            .button_count = 0
        };
        
        return mcp_tool_gpio_format_result(&error_result, result_json, result_size);
    }
    
    /* Execute action */
    mcp_gpio_result_t exec_result = {
        .success = true,
        .message = "OK",
        .pin_state = false,
        .pin_value = 0,
        .button_pressed = false,
        .button_count = 0
    };
    
    switch (params.action) {
        case MCP_GPIO_ACTION_SET_LED:
            {
                gpio_num_t led_pin = (gpio_num_t)MCP_GPIO_LED;
                ret = gpio_set_level(led_pin, params.state ? 1 : 0);
                if (ret != ESP_OK) {
                    exec_result.success = false;
                    exec_result.message = "Failed to set LED";
                } else {
                    exec_result.pin_state = params.state;
                    exec_result.pin_value = params.state ? 1 : 0;
                }
                ESP_LOGI(TAG, "Set LED to %s", params.state ? "ON" : "OFF");
            }
            break;
            
        case MCP_GPIO_ACTION_READ_BUTTON:
            {
                gpio_num_t button_pin = (gpio_num_t)MCP_GPIO_BUTTON;
                int level = gpio_get_level(button_pin);
                exec_result.pin_state = (level == 0); // Button is active low
                exec_result.pin_value = level;
                exec_result.button_pressed = exec_result.pin_state;
                exec_result.button_count = get_button_press_count();
                ESP_LOGI(TAG, "Button state: %s, count: %d", 
                        exec_result.button_pressed ? "PRESSED" : "RELEASED",
                        exec_result.button_count);
            }
            break;
            
        case MCP_GPIO_ACTION_GET_STATUS:
            {
                /* Get status of both LED and button */
                gpio_num_t led_pin = (gpio_num_t)MCP_GPIO_LED;
                gpio_num_t button_pin = (gpio_num_t)MCP_GPIO_BUTTON;
                
                int led_level = gpio_get_level(led_pin);
                int button_level = gpio_get_level(button_pin);
                
                exec_result.pin_state = (led_level == 1);
                exec_result.pin_value = led_level;
                exec_result.button_pressed = (button_level == 0); // Active low
                exec_result.button_count = get_button_press_count();
                
                ESP_LOGI(TAG, "GPIO Status - LED: %s, Button: %s, Count: %d",
                        exec_result.pin_state ? "ON" : "OFF",
                        exec_result.button_pressed ? "PRESSED" : "RELEASED",
                        exec_result.button_count);
            }
            break;
            
        case MCP_GPIO_ACTION_SET_PIN:
            {
                gpio_num_t pin = (gpio_num_t)params.pin;
                ret = gpio_set_level(pin, params.state ? 1 : 0);
                if (ret != ESP_OK) {
                    exec_result.success = false;
                    exec_result.message = "Failed to set pin";
                } else {
                    exec_result.pin_state = params.state;
                    exec_result.pin_value = params.state ? 1 : 0;
                }
                ESP_LOGI(TAG, "Set GPIO%d to %s", params.pin, params.state ? "HIGH" : "LOW");
            }
            break;
            
        case MCP_GPIO_ACTION_READ_PIN:
            {
                gpio_num_t pin = (gpio_num_t)params.pin;
                int level = gpio_get_level(pin);
                exec_result.pin_state = (level == 1);
                exec_result.pin_value = level;
                ESP_LOGI(TAG, "Read GPIO%d: %s", params.pin, level ? "HIGH" : "LOW");
            }
            break;
            
        case MCP_GPIO_ACTION_CONFIG_PIN:
            {
                gpio_num_t pin = (gpio_num_t)params.pin;
                gpio_config_t io_conf = {};
                
                io_conf.pin_bit_mask = (1ULL << pin);
                io_conf.mode = mcp_gpio_int_to_mode(params.mode);
                io_conf.pull_up_en = (params.pull_mode == 1 || params.mode == 2) ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
                io_conf.pull_down_en = (params.pull_mode == 2 || params.mode == 3) ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE;
                io_conf.intr_type = GPIO_INTR_DISABLE;
                
                ret = gpio_config(&io_conf);
                if (ret != ESP_OK) {
                    exec_result.success = false;
                    exec_result.message = "Failed to configure pin";
                } else {
                    /* Read current state after configuration */
                    int level = gpio_get_level(pin);
                    exec_result.pin_state = (level == 1);
                    exec_result.pin_value = level;
                }
                ESP_LOGI(TAG, "Configured GPIO%d: mode=%d, pull=%d", 
                        params.pin, params.mode, params.pull_mode);
            }
            break;
            
        default:
            exec_result.success = false;
            exec_result.message = "Unknown action";
            ESP_LOGW(TAG, "Unknown GPIO action: %d", params.action);
            break;
    }
    
    /* Format and return result */
    return mcp_tool_gpio_format_result(&exec_result, result_json, result_size);
}