# TinyMCP Component for ESP32-C6
# This component ports TinyMCP (Model Context Protocol) server functionality
# to ESP32-C6 with ESP-IDF integration

idf_component_register(
    SRCS "src/mcp_server_simple.c"
         "src/mcp_tools_simple.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_timer
             freertos
             driver
             nvs_flash
             json
             esp_hw_support
             esp_system
             esp_common
             log
    PRIV_REQUIRES display
)

# Add component-specific definitions
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    ESP32_MCP_SERVER_SIMPLE=1
    MCP_MAX_MESSAGE_SIZE=4096
    MCP_TASK_STACK_SIZE=8192
)
