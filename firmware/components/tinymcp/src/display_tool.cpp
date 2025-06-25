/**
 * @file display_tool.cpp
 * @brief MCP Display Tool Implementation for ESP32-C6
 * 
 * This file implements the display control tool for MCP, allowing remote
 * control of the ST7789 TFT display through JSON-RPC commands.
 */

#include "mcp_tools.h"
#include "display_st7789.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_err.h"
#include "cJSON.h"

static const char *TAG = "MCP_DISPLAY_TOOL";

/* External display handle - defined in main firmware */
extern display_handle_t* get_display_handle(void);

/* Display Tool JSON Schema */
const char* MCP_TOOL_DISPLAY_SCHEMA = 
"{"
  "\"type\": \"object\","
  "\"properties\": {"
    "\"action\": {"
      "\"type\": \"string\","
      "\"enum\": [\"show_text\", \"clear\", \"set_brightness\", \"draw_rect\", \"draw_pixel\", \"get_info\", \"refresh\"],"
      "\"description\": \"Action to perform on the display\""
    "},"
    "\"text\": {"
      "\"type\": \"string\","
      "\"description\": \"Text to display (for show_text action)\""
    "},"
    "\"x\": {"
      "\"type\": \"integer\","
      "\"minimum\": 0,"
      "\"maximum\": 319,"
      "\"description\": \"X coordinate\""
    "},"
    "\"y\": {"
      "\"type\": \"integer\","
      "\"minimum\": 0,"
      "\"maximum\": 171,"
      "\"description\": \"Y coordinate\""
    "},"
    "\"width\": {"
      "\"type\": \"integer\","
      "\"minimum\": 1,"
      "\"maximum\": 320,"
      "\"description\": \"Width in pixels (for draw_rect)\""
    "},"
    "\"height\": {"
      "\"type\": \"integer\","
      "\"minimum\": 1,"
      "\"maximum\": 172,"
      "\"description\": \"Height in pixels (for draw_rect)\""
    "},"
    "\"color\": {"
      "\"type\": \"string\","
      "\"enum\": [\"black\", \"white\", \"red\", \"green\", \"blue\", \"yellow\", \"cyan\", \"magenta\"],"
      "\"description\": \"Color name\""
    "},"
    "\"bg_color\": {"
      "\"type\": \"string\","
      "\"enum\": [\"black\", \"white\", \"red\", \"green\", \"blue\", \"yellow\", \"cyan\", \"magenta\"],"
      "\"description\": \"Background color name\""
    "},"
    "\"brightness\": {"
      "\"type\": \"integer\","
      "\"minimum\": 0,"
      "\"maximum\": 100,"
      "\"description\": \"Brightness percentage (for set_brightness)\""
    "}"
  "},"
  "\"required\": [\"action\"]"
"}";

/* Helper function to convert color name to RGB565 */
static uint16_t mcp_display_name_to_color(const char* color_name)
{
    if (!color_name) return MCP_COLOR_WHITE;
    
    if (strcmp(color_name, "black") == 0) return MCP_COLOR_BLACK;
    if (strcmp(color_name, "white") == 0) return MCP_COLOR_WHITE;
    if (strcmp(color_name, "red") == 0) return MCP_COLOR_RED;
    if (strcmp(color_name, "green") == 0) return MCP_COLOR_GREEN;
    if (strcmp(color_name, "blue") == 0) return MCP_COLOR_BLUE;
    if (strcmp(color_name, "yellow") == 0) return MCP_COLOR_YELLOW;
    if (strcmp(color_name, "cyan") == 0) return MCP_COLOR_CYAN;
    if (strcmp(color_name, "magenta") == 0) return MCP_COLOR_MAGENTA;
    
    return MCP_COLOR_WHITE; // Default
}

/* Helper function to convert action string to enum */
static mcp_display_action_t mcp_display_string_to_action(const char* action_str)
{
    if (!action_str) return MCP_DISPLAY_ACTION_GET_INFO;
    
    if (strcmp(action_str, "show_text") == 0) return MCP_DISPLAY_ACTION_SHOW_TEXT;
    if (strcmp(action_str, "clear") == 0) return MCP_DISPLAY_ACTION_CLEAR;
    if (strcmp(action_str, "set_brightness") == 0) return MCP_DISPLAY_ACTION_SET_BRIGHTNESS;
    if (strcmp(action_str, "draw_rect") == 0) return MCP_DISPLAY_ACTION_DRAW_RECT;
    if (strcmp(action_str, "draw_pixel") == 0) return MCP_DISPLAY_ACTION_DRAW_PIXEL;
    if (strcmp(action_str, "get_info") == 0) return MCP_DISPLAY_ACTION_GET_INFO;
    if (strcmp(action_str, "refresh") == 0) return MCP_DISPLAY_ACTION_REFRESH;
    
    return MCP_DISPLAY_ACTION_GET_INFO; // Default
}

/* Parse display tool parameters from JSON */
esp_err_t mcp_tool_display_parse_params(const char* params_json, 
                                        mcp_display_params_t* params)
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
    memset(params, 0, sizeof(mcp_display_params_t));
    params->color = MCP_COLOR_WHITE;
    params->bg_color = MCP_COLOR_BLACK;
    params->brightness = 100;
    
    /* Parse action (required) */
    cJSON* action = cJSON_GetObjectItem(json, "action");
    if (!action || !cJSON_IsString(action)) {
        ESP_LOGE(TAG, "Missing or invalid action parameter");
        cJSON_Delete(json);
        return ESP_ERR_INVALID_ARG;
    }
    params->action = mcp_display_string_to_action(cJSON_GetStringValue(action));
    
    /* Parse optional parameters */
    cJSON* text = cJSON_GetObjectItem(json, "text");
    if (text && cJSON_IsString(text)) {
        params->text = cJSON_GetStringValue(text);
    }
    
    cJSON* x = cJSON_GetObjectItem(json, "x");
    if (x && cJSON_IsNumber(x)) {
        params->x = (int)cJSON_GetNumberValue(x);
    }
    
    cJSON* y = cJSON_GetObjectItem(json, "y");
    if (y && cJSON_IsNumber(y)) {
        params->y = (int)cJSON_GetNumberValue(y);
    }
    
    cJSON* width = cJSON_GetObjectItem(json, "width");
    if (width && cJSON_IsNumber(width)) {
        params->width = (int)cJSON_GetNumberValue(width);
    }
    
    cJSON* height = cJSON_GetObjectItem(json, "height");
    if (height && cJSON_IsNumber(height)) {
        params->height = (int)cJSON_GetNumberValue(height);
    }
    
    cJSON* color = cJSON_GetObjectItem(json, "color");
    if (color && cJSON_IsString(color)) {
        params->color = (mcp_display_color_t)mcp_display_name_to_color(cJSON_GetStringValue(color));
    }
    
    cJSON* bg_color = cJSON_GetObjectItem(json, "bg_color");
    if (bg_color && cJSON_IsString(bg_color)) {
        params->bg_color = (mcp_display_color_t)mcp_display_name_to_color(cJSON_GetStringValue(bg_color));
    }
    
    cJSON* brightness = cJSON_GetObjectItem(json, "brightness");
    if (brightness && cJSON_IsNumber(brightness)) {
        params->brightness = (int)cJSON_GetNumberValue(brightness);
    }
    
    cJSON_Delete(json);
    return ESP_OK;
}

/* Validate display tool parameters */
esp_err_t mcp_tool_display_validate_params(const mcp_display_params_t* params)
{
    if (!params) {
        return ESP_ERR_INVALID_ARG;
    }
    
    /* Validate coordinates */
    if (params->x < 0 || params->x >= 320) {
        ESP_LOGE(TAG, "Invalid X coordinate: %d", params->x);
        return ESP_ERR_INVALID_ARG;
    }
    
    if (params->y < 0 || params->y >= 172) {
        ESP_LOGE(TAG, "Invalid Y coordinate: %d", params->y);
        return ESP_ERR_INVALID_ARG;
    }
    
    /* Validate dimensions for rectangle */
    if (params->action == MCP_DISPLAY_ACTION_DRAW_RECT) {
        if (params->width <= 0 || params->height <= 0) {
            ESP_LOGE(TAG, "Invalid rectangle dimensions: %dx%d", params->width, params->height);
            return ESP_ERR_INVALID_ARG;
        }
        
        if (params->x + params->width > 320 || params->y + params->height > 172) {
            ESP_LOGE(TAG, "Rectangle exceeds display bounds");
            return ESP_ERR_INVALID_ARG;
        }
    }
    
    /* Validate brightness */
    if (params->brightness < 0 || params->brightness > 100) {
        ESP_LOGE(TAG, "Invalid brightness: %d", params->brightness);
        return ESP_ERR_INVALID_ARG;
    }
    
    return ESP_OK;
}

/* Format display tool result to JSON */
esp_err_t mcp_tool_display_format_result(const mcp_display_result_t* result,
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
        cJSON_AddNumberToObject(json, "display_width", result->display_width);
        cJSON_AddNumberToObject(json, "display_height", result->display_height);
        cJSON_AddNumberToObject(json, "brightness", result->brightness);
        cJSON_AddBoolToObject(json, "backlight_on", result->backlight_on);
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

/* Get display tool schema as JSON string */
const char* mcp_tool_display_get_schema(void)
{
    return MCP_TOOL_DISPLAY_SCHEMA;
}

/* Execute display tool */
esp_err_t mcp_tool_display_execute(const char* params_json, char* result_json, size_t result_size)
{
    if (!params_json || !result_json || result_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Executing display tool: %s", params_json);
    
    /* Parse parameters */
    mcp_display_params_t params;
    esp_err_t ret = mcp_tool_display_parse_params(params_json, &params);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to parse parameters: %s", esp_err_to_name(ret));
        
        mcp_display_result_t error_result = {
            .success = false,
            .message = "Invalid parameters",
            .display_width = 0,
            .display_height = 0,
            .brightness = 0,
            .backlight_on = false
        };
        
        return mcp_tool_display_format_result(&error_result, result_json, result_size);
    }
    
    /* Validate parameters */
    ret = mcp_tool_display_validate_params(&params);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Invalid parameters: %s", esp_err_to_name(ret));
        
        mcp_display_result_t error_result = {
            .success = false,
            .message = "Parameter validation failed",
            .display_width = 0,
            .display_height = 0,
            .brightness = 0,
            .backlight_on = false
        };
        
        return mcp_tool_display_format_result(&error_result, result_json, result_size);
    }
    
    /* Get display handle */
    display_handle_t* display = get_display_handle();
    if (!display) {
        ESP_LOGE(TAG, "Display not initialized");
        
        mcp_display_result_t error_result = {
            .success = false,
            .message = "Display not available",
            .display_width = 0,
            .display_height = 0,
            .brightness = 0,
            .backlight_on = false
        };
        
        return mcp_tool_display_format_result(&error_result, result_json, result_size);
    }
    
    /* Execute action */
    mcp_display_result_t exec_result = {
        .success = true,
        .message = "OK",
        .display_width = 320,
        .display_height = 172,
        .brightness = 100,
        .backlight_on = true
    };
    
    switch (params.action) {
        case MCP_DISPLAY_ACTION_SHOW_TEXT:
            if (params.text) {
                ret = display_printf(display, params.x, params.y, params.color, 
                                   params.bg_color, "%s", params.text);
                if (ret != ESP_OK) {
                    exec_result.success = false;
                    exec_result.message = "Failed to display text";
                }
                ESP_LOGI(TAG, "Displayed text: '%s' at (%d,%d)", params.text, params.x, params.y);
            } else {
                exec_result.success = false;
                exec_result.message = "Text parameter required";
            }
            break;
            
        case MCP_DISPLAY_ACTION_CLEAR:
            ret = display_clear(display, params.color);
            if (ret != ESP_OK) {
                exec_result.success = false;
                exec_result.message = "Failed to clear display";
            }
            ESP_LOGI(TAG, "Cleared display with color: 0x%04X", params.color);
            break;
            
        case MCP_DISPLAY_ACTION_SET_BRIGHTNESS:
            {
                bool backlight_on = params.brightness > 0;
                ret = display_backlight_set(display, backlight_on);
                if (ret != ESP_OK) {
                    exec_result.success = false;
                    exec_result.message = "Failed to set brightness";
                } else {
                    exec_result.brightness = params.brightness;
                    exec_result.backlight_on = backlight_on;
                }
                ESP_LOGI(TAG, "Set brightness: %d%%", params.brightness);
            }
            break;
            
        case MCP_DISPLAY_ACTION_DRAW_RECT:
            ret = display_fill_rect(display, params.x, params.y, 
                                  params.width, params.height, params.color);
            if (ret != ESP_OK) {
                exec_result.success = false;
                exec_result.message = "Failed to draw rectangle";
            }
            ESP_LOGI(TAG, "Drew rectangle at (%d,%d) size %dx%d", 
                    params.x, params.y, params.width, params.height);
            break;
            
        case MCP_DISPLAY_ACTION_DRAW_PIXEL:
            ret = display_draw_pixel(display, params.x, params.y, params.color);
            if (ret != ESP_OK) {
                exec_result.success = false;
                exec_result.message = "Failed to draw pixel";
            }
            ESP_LOGI(TAG, "Drew pixel at (%d,%d)", params.x, params.y);
            break;
            
        case MCP_DISPLAY_ACTION_GET_INFO:
            /* Return display information - already set in exec_result */
            ESP_LOGI(TAG, "Returned display info");
            break;
            
        case MCP_DISPLAY_ACTION_REFRESH:
            /* Force display refresh - no specific API call needed for ST7789 */
            ESP_LOGI(TAG, "Display refresh requested");
            break;
            
        default:
            exec_result.success = false;
            exec_result.message = "Unknown action";
            ESP_LOGW(TAG, "Unknown display action: %d", params.action);
            break;
    }
    
    /* Format and return result */
    return mcp_tool_display_format_result(&exec_result, result_json, result_size);
}