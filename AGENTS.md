# AGENTS.md - ESP32-C6 Development Environment & Lessons Learned

## Overview
This document captures the essential knowledge and lessons learned while setting up a comprehensive ESP32-C6 development environment for the end-to-end tutorial. Future agents working on this project MUST read this document first.

## Environment Setup (WORKING)

### System Information
- **Host OS**: Ubuntu 24.04
- **IDF Version**: v5.4.1 (latest LTS for C6)
- **Board**: ESP32-C6-DevKit-1 on /dev/ttyACM0
- **Shell**: /bin/bash

### ESP-IDF Installation Process (VERIFIED WORKING)

1. **Prerequisites Installation** ✅
```bash
sudo apt-get update
sudo apt-get install -y git wget flex bison gperf python3 python3-pip python3-venv python3-setuptools cmake ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0 build-essential
```

2. **ESP-IDF Setup** ✅
```bash
# ESP-IDF is installed at: /home/mike/esp/esp-idf
# Version: v5.4.1 (confirmed working)
cd /home/mike/esp/esp-idf
./install.sh esp32c6
```

3. **Environment Activation** ✅
```bash
export IDF_PATH=/home/mike/esp/esp-idf
source $IDF_PATH/export.sh
```

4. **Verification** ✅
```bash
idf.py --version
# Should output: ESP-IDF v5.4.1
```

### Project Creation & Build Process (VERIFIED WORKING)

1. **Basic Project Creation** ✅
```bash
idf.py create-project hello_c6
cd hello_c6
idf.py set-target esp32c6
idf.py build
```

2. **Multi-Component Project Structure** ✅
```
firmware/
├── CMakeLists.txt
├── sdkconfig.defaults
├── main/
│   ├── CMakeLists.txt
│   └── firmware.c
└── components/
    ├── net_wifi/
    │   ├── CMakeLists.txt
    │   ├── include/
    │   └── wifi.c
    ├── net_ble/
    │   ├── CMakeLists.txt
    │   ├── include/
    │   └── ble_gatt_server.c
    └── lp_core/
        ├── CMakeLists.txt
        ├── include/
        └── lp_main.c
```

## CRITICAL LESSONS LEARNED

### ❌ MAJOR MISTAKES TO AVOID

1. **DO NOT WRITE CUSTOM DRIVERS**
   - ESP-IDF already has comprehensive driver components
   - Available at: https://github.com/espressif/esp-idf/tree/master/components
   - Use existing components: `esp_driver_gpio`, `esp_driver_spi`, `esp_driver_i2c`, etc.

2. **DO NOT REINVENT EXISTING FUNCTIONALITY**
   - Wi-Fi: Use `esp_wifi` component and examples
   - BLE: Use `bt` component and existing GATT examples
   - Thread/Zigbee: Use `openthread` and official Zigbee SDK
   - ULP: Use `ulp` component with proper LP core examples

### ✅ WHAT WORKS AND SHOULD BE USED

#### Wi-Fi 6 Implementation
- **Component to use**: `esp_wifi` (built-in)
- **Example location**: `$IDF_PATH/examples/wifi/`
- **Key features**: WPA3-SAE, 802.11ax support built-in
- **Configuration**: Use `idf.py menuconfig → Component config → Wi-Fi`

#### BLE 5.0 Implementation  
- **Component to use**: `bt` (built-in)
- **Example location**: `$IDF_PATH/examples/bluetooth/bluedroid/ble/`
- **Best starting point**: `gatt_server_service_table` example
- **Configuration**: Enable in menuconfig, coexistence works with Wi-Fi

#### Thread/OpenThread
- **Component to use**: `openthread` (built-in)
- **Add dependency**: `idf.py add-dependency "openthread/esp_openthread=*"`
- **Example location**: `$IDF_PATH/examples/openthread/`
- **Mode**: Use RCP (Radio Co-Processor) mode

#### Zigbee
- **Component**: External Zigbee SDK required
- **Repository**: https://github.com/espressif/esp-zigbee-sdk.git
- **Integration**: Add as git submodule
- **Examples**: In the Zigbee SDK repository

#### LP/ULP Core
- **Component to use**: `ulp` (built-in)
- **Examples**: `$IDF_PATH/examples/system/ulp/lp_core/`
- **Key function**: `ulp_lp_core_build_binary()` for building LP core binaries
- **Shared memory**: Use volatile variables for main CPU ↔ LP core communication

## WORKING COMPONENT EXAMPLES

### Component CMakeLists.txt Structure
```cmake
# For custom components using existing ESP-IDF functionality
idf_component_register(SRCS "source_file.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_wifi esp_netif esp_event nvs_flash)
```

### LP Core Integration
```cmake
# In lp_core component CMakeLists.txt
idf_component_register(SRCS "lp_core_main.c"
                       INCLUDE_DIRS "include"
                       REQUIRES ulp)

set(lp_sources "lp_main.c")
ulp_lp_core_build_binary(lp_main "${lp_sources}")
```

## NEXT STEPS FOR FUTURE AGENTS

### 1. Use Existing Examples First
Before writing ANY custom code:
1. Check `$IDF_PATH/examples/` for relevant examples
2. Copy and modify existing examples rather than writing from scratch
3. Reference official ESP-IDF documentation

### 2. Component Integration Strategy
1. **Wi-Fi**: Start with `examples/wifi/getting_started/station`
2. **BLE**: Start with `examples/bluetooth/bluedroid/ble/gatt_server_service_table`
3. **Thread**: Start with `examples/openthread/ot_rcp`
4. **LP Core**: Start with `examples/system/ulp/lp_core/gpio`

### 3. Configuration Management
- Use `idf.py menuconfig` for enabling features
- Create `sdkconfig.defaults` for project defaults
- Enable coexistence for multiple radio protocols

### 4. Security Features
- Configure in menuconfig under "Security features"
- Secure Boot V2 and Flash Encryption available for C6
- Don't enable JTAG after secure boot

## CURRENT PROJECT STATE

### What's Working ✅
- ESP-IDF v5.4.1 installation and environment
- Basic project creation and build system
- Component structure framework
- Target set to esp32c6

### What Needs Cleanup ❌
- Custom driver components (delete these)
- Over-engineered component implementations
- Non-standard component structures

### What Needs Implementation ✅
- Integration of existing ESP-IDF examples
- Proper use of built-in components
- Main application logic tying components together
- OTA and security configuration

## RESOURCES & REFERENCES

### Essential Documentation
- ESP-IDF Programming Guide: https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/
- ESP32-C6 Technical Reference: https://www.espressif.com/sites/default/files/documentation/esp32-c6_technical_reference_manual_en.pdf
- ESP-IDF Examples: Local at `$IDF_PATH/examples/`

### Key Commands Reference
```bash
# Environment setup
export IDF_PATH=/home/mike/esp/esp-idf && source $IDF_PATH/export.sh

# Project commands
idf.py create-project <name>
idf.py set-target esp32c6
idf.py menuconfig
idf.py build
idf.py -p /dev/ttyACM0 flash monitor

# Component management
idf.py add-dependency "component/name=*"
```

## FINAL NOTE TO FUTURE AGENTS

**ALWAYS USE EXISTING ESP-IDF COMPONENTS AND EXAMPLES FIRST!**

The ESP-IDF framework is mature and comprehensive. There's almost always an existing component or example that does what you need. Spend time understanding and adapting existing code rather than writing from scratch.

When in doubt, check the official examples and documentation before implementing anything custom.