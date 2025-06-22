#!/bin/bash

# ESP32-C6 Flash and Monitor Convenience Script
# Usage: ./esp_flash.sh [command]
# Commands: build, flash, monitor, flash-monitor, clean, menuconfig

set -e  # Exit on any error

# Configuration
ESP_IDF_PATH="/home/mike/esp/esp-idf"
FIRMWARE_DIR="$(dirname "$0")/firmware"
DEVICE_PORT="/dev/ttyACM0"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Function to print colored output
print_status() {
    echo -e "${BLUE}[ESP-C6]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

# Function to check if ESP-IDF is set up
check_esp_idf() {
    if [ ! -d "$ESP_IDF_PATH" ]; then
        print_error "ESP-IDF not found at $ESP_IDF_PATH"
        print_error "Please install ESP-IDF first or update ESP_IDF_PATH in this script"
        exit 1
    fi
}

# Function to setup ESP-IDF environment
setup_environment() {
    print_status "Setting up ESP-IDF environment..."
    export IDF_PATH="$ESP_IDF_PATH"
    source "$ESP_IDF_PATH/export.sh"
}

# Function to check if device is connected
check_device() {
    if [ ! -e "$DEVICE_PORT" ]; then
        print_error "Device not found at $DEVICE_PORT"
        print_error "Please connect your ESP32-C6 device"
        exit 1
    fi
    print_status "Device found at $DEVICE_PORT"
}

# Function to change to firmware directory
goto_firmware_dir() {
    if [ ! -d "$FIRMWARE_DIR" ]; then
        print_error "Firmware directory not found at $FIRMWARE_DIR"
        exit 1
    fi
    cd "$FIRMWARE_DIR"
    print_status "Changed to firmware directory: $(pwd)"
}

# Function to show usage
show_usage() {
    echo "ESP32-C6 Development Helper Script"
    echo ""
    echo "Usage: $0 [command]"
    echo ""
    echo "Commands:"
    echo "  build         - Build the firmware"
    echo "  flash         - Flash firmware to device"
    echo "  monitor       - Monitor serial output"
    echo "  flash-monitor - Flash and then monitor (default)"
    echo "  clean         - Clean build files"
    echo "  menuconfig    - Open configuration menu"
    echo "  erase         - Erase flash completely"
    echo "  size          - Show firmware size information"
    echo ""
    echo "Examples:"
    echo "  $0                    # Flash and monitor (default)"
    echo "  $0 build             # Build only"
    echo "  $0 flash             # Flash only"
    echo "  $0 monitor           # Monitor only"
    echo ""
    echo "Configuration:"
    echo "  ESP-IDF Path: $ESP_IDF_PATH"
    echo "  Firmware Dir: $FIRMWARE_DIR"
    echo "  Device Port:  $DEVICE_PORT"
}

# Main execution
main() {
    local command="${1:-flash-monitor}"

    print_status "ESP32-C6 Development Helper"
    print_status "Command: $command"

    # Check prerequisites
    check_esp_idf
    goto_firmware_dir
    setup_environment

    case "$command" in
        "build")
            print_status "Building firmware..."
            idf.py build
            print_success "Build completed successfully"
            ;;

        "flash")
            check_device
            print_status "Flashing firmware to device..."
            idf.py -p "$DEVICE_PORT" flash
            print_success "Flash completed successfully"
            ;;

        "monitor")
            check_device
            print_status "Starting monitor..."
            print_warning "Press Ctrl+] to exit monitor"
            idf.py -p "$DEVICE_PORT" monitor
            ;;

        "flash-monitor")
            check_device
            print_status "Flashing firmware and starting monitor..."
            idf.py -p "$DEVICE_PORT" flash
            print_success "Flash completed successfully"
            print_status "Starting monitor..."
            print_warning "Press Ctrl+] to exit monitor"
            idf.py -p "$DEVICE_PORT" monitor
            ;;

        "clean")
            print_status "Cleaning build files..."
            idf.py clean
            print_success "Clean completed successfully"
            ;;

        "menuconfig")
            print_status "Opening configuration menu..."
            idf.py menuconfig
            ;;

        "erase")
            check_device
            print_warning "This will erase all flash memory!"
            read -p "Are you sure? (y/N): " -n 1 -r
            echo
            if [[ $REPLY =~ ^[Yy]$ ]]; then
                print_status "Erasing flash..."
                idf.py -p "$DEVICE_PORT" erase-flash
                print_success "Flash erased successfully"
            else
                print_status "Erase cancelled"
            fi
            ;;

        "size")
            print_status "Analyzing firmware size..."
            idf.py size
            ;;

        "help"|"-h"|"--help")
            show_usage
            ;;

        *)
            print_error "Unknown command: $command"
            echo ""
            show_usage
            exit 1
            ;;
    esac
}

# Check if script is being sourced or executed
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi
