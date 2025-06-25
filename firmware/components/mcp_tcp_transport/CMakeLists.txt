idf_component_register(
    SRCS "src/mcp_tcp_transport.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_timer freertos driver nvs_flash json esp_hw_support
             esp_system esp_common log lwip esp_netif esp_wifi
    PRIV_REQUIRES tinymcp wifi_manager
)

# Component-specific definitions
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    MCP_TCP_SERVER_PORT=8080
    MCP_TCP_MAX_CLIENTS=4
    MCP_TCP_BUFFER_SIZE=2048
    MCP_TCP_TASK_STACK_SIZE=8192
    MCP_TCP_TASK_PRIORITY=6
)
