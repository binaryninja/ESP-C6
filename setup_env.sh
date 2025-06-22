#!/bin/bash

# ESP32-C6 Development Environment Setup
# Quick setup script for ESP-IDF environment

set -e

# Configuration
ESP_IDF_PATH="/home/mike/esp/esp-idf"

# Colors for output
GREEN='\033[0;32m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

print_status() {
    echo -e "${BLUE}[ESP-C6]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

# Check if ESP-IDF is installed
if [ ! -d "$ESP_IDF_PATH" ]; then
    echo "ERROR: ESP-IDF not found at $ESP_IDF_PATH"
    echo "Please run ./setup_esp32c6_sdk.sh first"
    exit 1
fi

# Setup environment
print_status "Setting up ESP-IDF environment..."
export IDF_PATH="$ESP_IDF_PATH"
source "$ESP_IDF_PATH/export.sh"

print_success "ESP32-C6 development environment ready!"
echo ""
echo "Available commands:"
echo "  idf.py build                     - Build the firmware"
echo "  idf.py -p /dev/ttyACM0 flash     - Flash to device"
echo "  idf.py -p /dev/ttyACM0 monitor   - Monitor serial output"
echo ""
echo "Or use the convenience script:"
echo "  ./esp_flash.sh build             - Build only"
echo "  ./esp_flash.sh flash             - Flash only"
echo "  ./esp_flash.sh monitor           - Monitor only"
echo "  ./esp_flash.sh                   - Flash and monitor (default)"
