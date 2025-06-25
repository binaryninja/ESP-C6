idf_component_register(
    SRCS "src/wifi_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_wifi
        esp_netif
        esp_event
        esp_timer
        lwip
        log
        freertos
    PRIV_REQUIRES
        esp_system
)

# Component-specific definitions
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    WIFI_MANAGER_VERSION="1.0.0"
)
