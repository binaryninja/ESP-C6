#!/bin/bash

# ESP32 C6 Development Environment Setup Script
# This script installs ESP-IDF for ESP32 C6 development

set -e

echo "🚀 Setting up ESP32 C6 development environment..."

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Configuration
ESP_IDF_DIR="/home/$USER/esp/esp-idf"
ESP_IDF_VERSION="v5.4.1"  # Latest stable version that supports ESP32 C6

# Function to print colored output
print_status() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check if running on supported system
if [[ "$OSTYPE" != "linux-gnu"* ]]; then
    print_error "This script is designed for Linux systems only."
    exit 1
fi

# Create esp directory if it doesn't exist
print_status "Creating ESP development directory..."
mkdir -p /home/$USER/esp

# Install prerequisites
print_status "Installing prerequisites..."
sudo apt-get update
sudo apt-get install -y \
    git \
    wget \
    flex \
    bison \
    gperf \
    python3 \
    python3-pip \
    python3-setuptools \
    python3-serial \
    python3-cryptography \
    python3-future \
    python3-pyparsing \
    python3-pyelftools \
    cmake \
    ninja-build \
    ccache \
    libffi-dev \
    libssl-dev \
    dfu-util \
    libusb-1.0-0 \
    build-essential

# Check if ESP-IDF is already installed
if [ -d "$ESP_IDF_DIR" ]; then
    print_warning "ESP-IDF directory already exists at $ESP_IDF_DIR"
    read -p "Do you want to remove it and reinstall? (y/N): " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        print_status "Removing existing ESP-IDF installation..."
        rm -rf "$ESP_IDF_DIR"
    else
        print_status "Keeping existing installation. Checking for updates..."
        cd "$ESP_IDF_DIR"
        git fetch
        git checkout "$ESP_IDF_VERSION"
        git submodule update --init --recursive
        ./install.sh esp32c6
        print_status "ESP-IDF updated and ready!"
        exit 0
    fi
fi

# Clone ESP-IDF
print_status "Cloning ESP-IDF $ESP_IDF_VERSION..."
cd /home/$USER/esp
git clone --recursive --branch $ESP_IDF_VERSION https://github.com/espressif/esp-idf.git

# Navigate to ESP-IDF directory
cd esp-idf

# Install ESP-IDF tools for ESP32 C6
print_status "Installing ESP-IDF tools for ESP32 C6..."
./install.sh esp32c6

# Create environment setup script
print_status "Creating environment setup script..."
cat > /home/$USER/esp/setup_esp32c6_env.sh << EOF
#!/bin/bash
# ESP32 C6 Development Environment Setup

export IDF_PATH=/home/$USER/esp/esp-idf
source \$IDF_PATH/export.sh

echo "ESP32 C6 development environment ready!"
echo "ESP-IDF Path: \$IDF_PATH"
echo "ESP32 C6 toolchain loaded."
echo ""
echo "Usage:"
echo "  idf.py set-target esp32c6"
echo "  idf.py build"
echo "  idf.py -p /dev/ttyACM0 flash monitor"
EOF

chmod +x /home/$USER/esp/setup_esp32c6_env.sh

# Add to bashrc if not already present
if ! grep -q "setup_esp32c6_env.sh" /home/$USER/.bashrc; then
    print_status "Adding ESP32 C6 environment setup to .bashrc..."
    echo "" >> /home/$USER/.bashrc
    echo "# ESP32 C6 Development Environment" >> /home/$USER/.bashrc
    echo "# Uncomment the following line to auto-load ESP32 C6 environment:" >> /home/$USER/.bashrc
    echo "# source /home/$USER/esp/setup_esp32c6_env.sh" >> /home/$USER/.bashrc
fi

# Test the installation
print_status "Testing ESP-IDF installation..."
source /home/$USER/esp/setup_esp32c6_env.sh

# Check if idf.py is available
if command -v idf.py &> /dev/null; then
    print_status "ESP-IDF installation successful!"
    idf.py --version
else
    print_error "ESP-IDF installation failed. idf.py not found."
    exit 1
fi

# Check ESP32 C6 support
print_status "Checking ESP32 C6 support..."
if idf.py --list-targets | grep -q "esp32c6"; then
    print_status "ESP32 C6 target is supported!"
else
    print_warning "ESP32 C6 target not found in supported targets list."
fi

# Final instructions
echo ""
echo "🎉 ESP32 C6 development environment setup complete!"
echo ""
echo "To activate the environment in a new terminal:"
echo "  source /home/$USER/esp/setup_esp32c6_env.sh"
echo ""
echo "Or add it to your .bashrc to auto-load:"
echo "  echo 'source /home/$USER/esp/setup_esp32c6_env.sh' >> ~/.bashrc"
echo ""
echo "To build the ESP32 C6 version of this project:"
echo "  cd ESP8266-mcp"
echo "  git checkout esp32-c6-support"
echo "  source /home/$USER/esp/setup_esp32c6_env.sh"
echo "  idf.py set-target esp32c6"
echo "  idf.py build"
echo "  idf.py -p /dev/ttyACM0 flash monitor"
echo ""
echo "Note: Your ESP32 C6 should appear as /dev/ttyACM0 when connected."
echo "      Check with 'dmesg | tail' if you're unsure."
