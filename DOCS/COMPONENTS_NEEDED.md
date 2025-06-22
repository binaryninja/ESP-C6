# ESP32-C6 Component Dependencies

## Overview
This document lists all ESP-IDF components needed for the comprehensive ESP32-C6 development project. These are built-in components from the ESP-IDF framework that should be used instead of writing custom implementations.

## Core System Components

### Essential System Components
- `esp_system` - Core system functionality
- `esp_common` - Common ESP-IDF utilities
- `esp_hw_support` - Hardware abstraction layer
- `esp_timer` - High-resolution timer API
- `esp_event` - Event loop library
- `log` - Logging functionality
- `heap` - Memory management
- `soc` - SoC-specific definitions
- `hal` - Hardware Abstraction Layer
- `efuse` - eFuse API for chip configuration

### Memory & Storage
- `nvs_flash` - Non-volatile storage
- `nvs_sec_provider` - NVS security provider
- `spi_flash` - SPI flash access
- `partition_table` - Partition management
- `wear_levelling` - Flash wear levelling
- `fatfs` - FAT filesystem support
- `spiffs` - SPIFFS filesystem support
- `vfs` - Virtual file system

### Boot & Application
- `bootloader` - Bootloader functionality
- `bootloader_support` - Bootloader support utilities
- `app_update` - OTA application updates
- `esp_app_format` - Application format handling
- `esp_bootloader_format` - Bootloader format handling

## Networking Components

### Wi-Fi 6 Support
- `esp_wifi` - **Primary Wi-Fi component (802.11ax/Wi-Fi 6)**
- `esp_netif` - Network interface abstraction
- `esp_netif_stack` - Network stack implementation
- `lwip` - Lightweight IP stack
- `wpa_supplicant` - WPA/WPA2/WPA3 security
- `wifi_provisioning` - Wi-Fi provisioning
- `esp_coex` - Wi-Fi/BT coexistence

### Bluetooth 5.0 Support
- `bt` - **Primary Bluetooth component (BLE 5.0)**
- `esp_hid` - HID over GATT support

### Thread/Matter Support
- `openthread` - **OpenThread implementation**
- `ieee802154` - IEEE 802.15.4 radio support

### HTTP/HTTPS & Web Services
- `esp_http_client` - HTTP client
- `esp_http_server` - HTTP server
- `esp_https_ota` - HTTPS OTA updates
- `esp_https_server` - HTTPS server
- `esp-tls` - TLS/SSL support
- `mbedtls` - Cryptographic library
- `tcp_transport` - TCP transport layer

### Other Networking
- `esp_eth` - Ethernet support
- `mqtt` - MQTT client
- `json` - JSON parsing
- `protobuf-c` - Protocol buffers
- `protocomm` - Protocol communication

## Hardware Driver Components

### GPIO & Basic I/O
- `esp_driver_gpio` - **GPIO operations**
- `esp_driver_uart` - **UART communication**
- `esp_driver_usb_serial_jtag` - USB Serial/JTAG

### Communication Interfaces
- `esp_driver_spi` - **SPI master/slave**
- `esp_driver_i2c` - **I2C master/slave**
- `esp_driver_i2s` - I2S audio interface
- `esp_driver_parlio` - Parallel I/O
- `esp_driver_sdmmc` - SD/MMC interface
- `esp_driver_sdspi` - SD card over SPI
- `esp_driver_sdio` - SDIO interface

### Timers & PWM
- `esp_driver_gptimer` - General purpose timers
- `esp_driver_ledc` - **LED PWM controller**
- `esp_driver_mcpwm` - Motor control PWM
- `esp_driver_pcnt` - Pulse counter
- `esp_driver_rmt` - Remote control transceiver

### Analog & Sensors
- `esp_adc` - **ADC (Analog-to-Digital Converter)**
- `esp_driver_dac` - DAC (Digital-to-Analog Converter)
- `esp_driver_ana_cmpr` - Analog comparator
- `esp_driver_tsens` - Temperature sensor
- `esp_driver_touch_sens` - Touch sensor

### Advanced Peripherals
- `esp_driver_cam` - Camera interface
- `esp_driver_jpeg` - JPEG encoder/decoder
- `esp_driver_isp` - Image signal processor
- `esp_driver_ppa` - Pixel processing accelerator
- `esp_driver_sdm` - Sigma-delta modulator

### Legacy Driver Support
- `driver` - Legacy driver component (compatibility)

## Ultra-Low Power (ULP) Components

### LP Core Support
- `ulp` - **Ultra Low Power coprocessor**
- `esp_pm` - **Power management**

## Security Components

### Cryptography & Security
- `esp_security` - Security utilities
- `mbedtls` - TLS/SSL and crypto library

## Development & Debug Components

### Testing & Debug
- `unity` - Unit testing framework
- `cmock` - Mocking framework for testing
- `console` - Console command interface
- `esp_gdbstub` - GDB stub for debugging
- `app_trace` - Application tracing
- `espcoredump` - Core dump functionality
- `perfmon` - Performance monitoring
- `idf_test` - IDF testing utilities

### Build System
- `esptool_py` - Flash tool integration

## Platform-Specific Components

### Architecture Support
- `riscv` - RISC-V architecture support (ESP32-C6 uses RISC-V)
- `freertos` - Real-time operating system
- `newlib` - C library implementation
- `pthread` - POSIX threads
- `cxx` - C++ support
- `rt` - Runtime support

## Component Usage in CMakeLists.txt

### For Main Application
```cmake
idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES 
        # Core system
        esp_system esp_event esp_timer log nvs_flash
        # Networking
        esp_wifi esp_netif lwip esp_http_server esp_https_ota
        # Bluetooth
        bt
        # Thread/Matter
        openthread ieee802154
        # Drivers
        esp_driver_gpio esp_driver_uart esp_driver_spi esp_driver_i2c
        # Power management
        esp_pm
        # ULP
        ulp
)
```

### For Custom Components
```cmake
idf_component_register(
    SRCS "component_source.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi esp_netif esp_event nvs_flash esp_driver_gpio
    PRIV_REQUIRES esp_http_client esp-tls
)
```

## Project Dependencies Command

Add all necessary dependencies to your project:

```bash
# Core networking and system
idf.py add-dependency "espressif/esp_wifi=*"
idf.py add-dependency "espressif/esp_netif=*"
idf.py add-dependency "espressif/esp_event=*"
idf.py add-dependency "espressif/nvs_flash=*"

# Bluetooth
idf.py add-dependency "espressif/bt=*"

# OpenThread for Thread/Matter
idf.py add-dependency "espressif/openthread=*"

# Essential drivers
idf.py add-dependency "espressif/esp_driver_gpio=*"
idf.py add-dependency "espressif/esp_driver_uart=*"
idf.py add-dependency "espressif/esp_driver_spi=*"
idf.py add-dependency "espressif/esp_driver_i2c=*"

# HTTP/HTTPS and OTA
idf.py add-dependency "espressif/esp_http_server=*"
idf.py add-dependency "espressif/esp_https_ota=*"

# ULP and power management
idf.py add-dependency "espressif/ulp=*"
idf.py add-dependency "espressif/esp_pm=*"
```

## Priority Components for ESP32-C6 Project

### Immediate Priority (Core functionality)
1. `esp_wifi` - Wi-Fi 6 support
2. `bt` - Bluetooth 5.0 support
3. `esp_netif` - Network interface
4. `esp_event` - Event system
5. `nvs_flash` - Configuration storage
6. `esp_driver_gpio` - GPIO control
7. `esp_driver_uart` - Serial communication

### High Priority (Advanced features)
1. `openthread` - Thread/Matter networking
2. `ulp` - Ultra-low power functionality
3. `esp_pm` - Power management
4. `esp_https_ota` - Secure OTA updates
5. `esp_http_server` - Web server functionality

### Medium Priority (Extended functionality)
1. `esp_driver_spi` - SPI communication
2. `esp_driver_i2c` - I2C communication
3. `esp_adc` - Analog measurements
4. `esp_driver_ledc` - PWM control
5. `esp_security` - Security features

## Notes

- All components listed are **built-in** to ESP-IDF v5.4.1
- These components are **actively maintained** by Espressif
- **No custom implementations needed** - use these instead
- Components are optimized for ESP32-C6 architecture
- Coexistence between Wi-Fi, Bluetooth, and Thread is supported
- ULP component supports LP core functionality specific to ESP32-C6

## External Dependencies

### Zigbee Support (Separate SDK)
- Repository: https://github.com/espressif/esp-zigbee-sdk.git
- Integration: Add as git submodule to project
- Location: Should be added to project root as `esp-zigbee-sdk/`

```bash
# Add Zigbee SDK as submodule
git submodule add https://github.com/espressif/esp-zigbee-sdk.git esp-zigbee-sdk
git submodule update --init --recursive
```
