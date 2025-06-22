idf_component_register(
    SRCS "firmware.c"
    INCLUDE_DIRS "."
    REQUIRES
        # Core system components
        esp_system
        esp_event
        esp_timer
        log
        nvs_flash

        # Networking components
        esp_wifi
        esp_netif
        lwip
        esp_http_server
        esp_https_ota

        # Bluetooth support
        bt

        # Thread/Matter support
        openthread
        ieee802154

        # Essential drivers
        esp_driver_gpio
        esp_driver_uart
        esp_driver_spi
        esp_driver_i2c
        esp_adc
        esp_driver_ledc

        # Power management & ULP
        esp_pm
        ulp

        # Flash operations
        spi_flash

        # Security
        esp_security
        mbedtls
)
