#!/bin/bash

# ESP32-C6 Development Monitor Script with Correlated Client/Server Logging
# Usage: ./dev_monitor.sh [command] [options]
#
# This script provides enhanced development capabilities for ESP32-C6 firmware:
# - Build, flash, and monitor firmware
# - Dual-pane monitoring with server and client logs
# - Stub client functionality for future webserver integration
# - Correlated logging with timestamps
# - Testing and validation tools

set -e  # Exit on any error

# Configuration
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ESP_IDF_PATH="/home/mike/esp/esp-idf"
FIRMWARE_DIR="$SCRIPT_DIR/firmware"
DEVICE_PORT="/dev/ttyACM0"
LOG_DIR="$SCRIPT_DIR/logs"
CLIENT_LOG_FILE="$LOG_DIR/client.log"
SERVER_LOG_FILE="$LOG_DIR/server.log"
COMBINED_LOG_FILE="$LOG_DIR/combined.log"

# Default configuration for future webserver
WEB_SERVER_IP="192.168.1.100"  # Will be ESP32-C6 IP when WiFi is implemented
WEB_SERVER_PORT="80"
HTTP_CLIENT_DELAY="5"  # seconds between client requests

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
MAGENTA='\033[0;35m'
WHITE='\033[1;37m'
NC='\033[0m' # No Color

# Function to print colored output with timestamps
timestamp() {
    date '+%Y-%m-%d %H:%M:%S'
}

print_status() {
    echo -e "${BLUE}[$(timestamp)] [DEV-MON]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[$(timestamp)] [SUCCESS]${NC} $1"
}

print_error() {
    echo -e "${RED}[$(timestamp)] [ERROR]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[$(timestamp)] [WARNING]${NC} $1"
}

print_client() {
    echo -e "${CYAN}[$(timestamp)] [CLIENT]${NC} $1"
}

print_server() {
    echo -e "${MAGENTA}[$(timestamp)] [SERVER]${NC} $1"
}

# Function to setup logging directory
setup_logging() {
    if [ ! -d "$LOG_DIR" ]; then
        mkdir -p "$LOG_DIR"
        print_status "Created log directory: $LOG_DIR"
    fi

    # Ensure log files exist
    touch "$CLIENT_LOG_FILE"
    touch "$SERVER_LOG_FILE"
    touch "$COMBINED_LOG_FILE"

    # Clear previous logs
    > "$CLIENT_LOG_FILE"
    > "$SERVER_LOG_FILE"
    > "$COMBINED_LOG_FILE"

    print_status "Logging initialized"
    print_status "Client log: $CLIENT_LOG_FILE"
    print_status "Server log: $SERVER_LOG_FILE"
    print_status "Combined log: $COMBINED_LOG_FILE"
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
    source "$ESP_IDF_PATH/export.sh" > /dev/null 2>&1
    print_success "ESP-IDF environment ready"
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

# Function to build firmware
build_firmware() {
    print_status "Building ESP32-C6 firmware..."
    if idf.py build > /dev/null 2>&1; then
        print_success "Firmware build completed successfully"

        # Show build information
        print_status "Build Information:"
        idf.py size | grep -E "(Total|Used|Free)" | while read line; do
            print_status "  $line"
        done
    else
        print_error "Firmware build failed"
        print_error "Run 'idf.py build' manually to see detailed error output"
        exit 1
    fi
}

# Function to flash firmware
flash_firmware() {
    check_device
    print_status "Flashing firmware to ESP32-C6..."
    if idf.py -p "$DEVICE_PORT" flash > /dev/null 2>&1; then
        print_success "Firmware flashed successfully"
        sleep 2  # Give device time to restart
    else
        print_error "Firmware flash failed"
        exit 1
    fi
}

# Function to reset ESP32-C6 device
reset_device() {
    check_device
    print_status "Resetting ESP32-C6 device..."
    if idf.py -p "$DEVICE_PORT" monitor --no-reset &
    then
        local monitor_pid=$!
        sleep 1
        # Send reset command
        if python3 -c "
import serial
import time
ser = serial.Serial('$DEVICE_PORT', 115200, timeout=1)
ser.setRTS(True)
ser.setDTR(False)
time.sleep(0.1)
ser.setRTS(False)
time.sleep(0.1)
ser.close()
" 2>/dev/null; then
            print_success "Device reset triggered"
            sleep 1
            kill $monitor_pid 2>/dev/null || true
        else
            print_warning "Reset via Python failed, trying ESP-IDF reset..."
            kill $monitor_pid 2>/dev/null || true
            # Alternative reset method
            esptool.py --port "$DEVICE_PORT" run > /dev/null 2>&1 || true
            print_success "Reset completed"
        fi
    else
        print_warning "Monitor startup failed, attempting direct reset..."
        esptool.py --port "$DEVICE_PORT" run > /dev/null 2>&1 || true
    fi
    sleep 1
}

# Stub client simulator for future webserver integration
simulate_http_client() {
    local test_duration=${1:-60}  # Default 60 seconds

    print_client "Starting HTTP client simulator (stub implementation)"
    print_client "Duration: ${test_duration} seconds"
    print_client "Target: http://${WEB_SERVER_IP}:${WEB_SERVER_PORT} (when implemented)"

    local start_time=$(date +%s)
    local request_count=0

    while [ $(($(date +%s) - start_time)) -lt $test_duration ]; do
        request_count=$((request_count + 1))

        # Simulate different types of HTTP requests that we might make to ESP32-C6
        case $((request_count % 4)) in
            0)
                print_client "GET /api/status - Requesting system status"
                echo "[$(timestamp)] [CLIENT] GET /api/status - System status request" >> "$CLIENT_LOG_FILE"
                ;;
            1)
                print_client "GET /api/display - Requesting display content"
                echo "[$(timestamp)] [CLIENT] GET /api/display - Display content request" >> "$CLIENT_LOG_FILE"
                ;;
            2)
                print_client "POST /api/led - Toggle LED command"
                echo "[$(timestamp)] [CLIENT] POST /api/led - LED toggle command" >> "$CLIENT_LOG_FILE"
                ;;
            3)
                print_client "GET /api/sensors - Requesting sensor data"
                echo "[$(timestamp)] [CLIENT] GET /api/sensors - Sensor data request" >> "$CLIENT_LOG_FILE"
                ;;
        esac

        # Simulate response (stub - will be real HTTP when webserver is implemented)
        sleep 1
        print_client "Response: 200 OK (simulated)"
        echo "[$(timestamp)] [CLIENT] Response: 200 OK (simulated)" >> "$CLIENT_LOG_FILE"

        # Log to combined log
        echo "[$(timestamp)] [CLIENT] Request #$request_count completed" >> "$COMBINED_LOG_FILE"

        sleep "$HTTP_CLIENT_DELAY"
    done

    print_client "Client simulation completed. Total requests: $request_count"
}

# Function to monitor serial output with enhanced logging
monitor_serial() {
    local log_file="$SERVER_LOG_FILE"

    print_server "Starting serial monitor..."
    print_server "Device: $DEVICE_PORT"
    print_server "Log file: $log_file"
    print_warning "Press Ctrl+C to stop monitoring"

    # Use direct serial reading instead of idf.py monitor
    print_server "Monitoring serial output..."
    python3 -c "
import serial
import sys
import time

try:
    ser = serial.Serial('$DEVICE_PORT', 115200, timeout=1)
    print('Connected to ESP32-C6 on $DEVICE_PORT')

    while True:
        try:
            line = ser.readline()
            if line:
                decoded = line.decode('utf-8', errors='ignore').strip()
                if decoded:
                    print(decoded)
                    sys.stdout.flush()
        except KeyboardInterrupt:
            break
        except Exception as e:
            print(f'Serial read error: {e}')
            time.sleep(0.1)

    ser.close()
    print('Serial monitor closed')
except Exception as e:
    print(f'Serial connection error: {e}')
" | while IFS= read -r line; do
        timestamp_now=$(date '+%Y-%m-%d %H:%M:%S')
        print_server "$line"
        echo "[$timestamp_now] [SERVER] $line" >> "$log_file"
        echo "[$timestamp_now] [SERVER] $line" >> "$COMBINED_LOG_FILE"
    done
}

# Function to monitor with reset for boot sequence capture
monitor_with_reset() {
    local duration=${1:-30}
    local log_file="$SERVER_LOG_FILE"

    print_server "Starting monitor with device reset..."
    print_server "Duration: $duration seconds"
    print_server "Device: $DEVICE_PORT"
    print_server "Log file: $log_file"

    # Start monitoring in background
    timeout "$duration" python3 -c "
import serial
import sys
import time
import threading
import signal

# Signal handler for clean exit
def signal_handler(sig, frame):
    global running
    running = False
    sys.exit(0)

signal.signal(signal.SIGINT, signal_handler)
signal.signal(signal.SIGTERM, signal_handler)

running = True

try:
    ser = serial.Serial('$DEVICE_PORT', 115200, timeout=1)
    print('[$DEVICE_PORT] Connected to ESP32-C6')

    while running:
        try:
            line = ser.readline()
            if line:
                decoded = line.decode('utf-8', errors='ignore').strip()
                if decoded:
                    timestamp = time.strftime('%Y-%m-%d %H:%M:%S')
                    print(f'[{timestamp}] [SERVER] {decoded}')
                    sys.stdout.flush()
        except KeyboardInterrupt:
            running = False
            break
        except Exception as e:
            print(f'Serial read error: {e}')
            time.sleep(0.1)

    ser.close()
    print('Serial monitor closed')
except Exception as e:
    print(f'Serial connection error: {e}')
" | while IFS= read -r line; do
        echo "$line" >> "$log_file"
        echo "$line" >> "$COMBINED_LOG_FILE"
        print_server "${line#*] [SERVER] }"
    done &
    local monitor_pid=$!

    # Give monitor time to start
    sleep 2

    # Reset the device to capture boot sequence
    print_status "Triggering device reset to capture boot sequence..."
    reset_device

    # Wait for monitoring to complete
    wait $monitor_pid 2>/dev/null || true

    print_success "Monitor with reset completed"
}

# Function to run dual monitoring (server + client simulation)
dual_monitor() {
    local duration=${1:-300}  # Default 5 minutes

    print_status "Starting dual monitoring mode"
    print_status "Duration: $duration seconds"
    print_status "Logs will be saved to: $LOG_DIR"

    # Start server monitoring in background first
    print_status "Starting server monitor..."
    timeout "$duration" python3 -c "
import serial
import sys
import time
import signal

def signal_handler(sig, frame):
    global running
    running = False
    sys.exit(0)

signal.signal(signal.SIGINT, signal_handler)
signal.signal(signal.SIGTERM, signal_handler)

running = True

try:
    ser = serial.Serial('$DEVICE_PORT', 115200, timeout=1)
    print('Connected to ESP32-C6 on $DEVICE_PORT')

    while running:
        try:
            line = ser.readline()
            if line:
                decoded = line.decode('utf-8', errors='ignore').strip()
                if decoded:
                    timestamp = time.strftime('%Y-%m-%d %H:%M:%S')
                    print(f'[{timestamp}] [SERVER] {decoded}')
                    sys.stdout.flush()
        except KeyboardInterrupt:
            running = False
            break
        except Exception as e:
            print(f'Serial read error: {e}')
            time.sleep(0.1)

    ser.close()
except Exception as e:
    print(f'Serial connection error: {e}')
" | while IFS= read -r line; do
        echo "$line" >> "$SERVER_LOG_FILE"
        echo "$line" >> "$COMBINED_LOG_FILE"
        print_server "${line#*] [SERVER] }"
    done &
    local monitor_pid=$!

    # Give monitor time to start
    sleep 2

    # Reset the device to capture fresh boot sequence
    print_status "Triggering device reset to capture boot sequence..."
    reset_device

    # Wait a moment for boot to start, then start client simulation
    sleep 3
    print_status "Starting client simulation..."
    simulate_http_client "$((duration - 5))" &
    local client_pid=$!

    # Wait for monitoring to complete
    wait $monitor_pid 2>/dev/null || true

    # Clean up client simulation if still running
    if ps -p $client_pid > /dev/null 2>&1; then
        kill $client_pid 2>/dev/null || true
        wait $client_pid 2>/dev/null || true
    fi

    print_success "Dual monitoring completed"
    show_log_summary
}

# Function to show log summary
show_log_summary() {
    print_status "=== LOG SUMMARY ==="

    if [ -f "$CLIENT_LOG_FILE" ]; then
        local client_lines=$(wc -l < "$CLIENT_LOG_FILE")
        print_status "Client log entries: $client_lines"
    fi

    if [ -f "$SERVER_LOG_FILE" ]; then
        local server_lines=$(wc -l < "$SERVER_LOG_FILE")
        print_status "Server log entries: $server_lines"
    fi

    if [ -f "$COMBINED_LOG_FILE" ]; then
        local combined_lines=$(wc -l < "$COMBINED_LOG_FILE")
        print_status "Combined log entries: $combined_lines"
    fi

    print_status "Log files available in: $LOG_DIR"
}

# Function to tail logs in real-time
tail_logs() {
    local log_type=${1:-combined}

    case "$log_type" in
        "client")
            print_status "Tailing client log..."
            tail -f "$CLIENT_LOG_FILE" 2>/dev/null || print_error "Client log not found"
            ;;
        "server")
            print_status "Tailing server log..."
            tail -f "$SERVER_LOG_FILE" 2>/dev/null || print_error "Server log not found"
            ;;
        "combined"|*)
            print_status "Tailing combined log..."
            tail -f "$COMBINED_LOG_FILE" 2>/dev/null || print_error "Combined log not found"
            ;;
    esac
}

# Function to test hardware functionality
test_hardware() {
    print_status "Running hardware functionality test..."

    # Build and flash first
    build_firmware
    flash_firmware

    print_status "Testing hardware components..."
    print_status "Expected behavior:"
    print_status "  - Status LED should blink every 1 second"
    print_status "  - Display should show 'Hello World' message"
    print_status "  - Button press should update display with system stats"
    print_status "  - Serial output should show system information"

    # Monitor with reset for 30 seconds to capture boot sequence
    print_status "Monitoring with reset for 30 seconds to verify functionality..."
    monitor_with_reset 30

    print_success "Hardware test completed - check logs for detailed output"
}

# Function to show comprehensive help
show_usage() {
    echo "ESP32-C6 Development Monitor Script"
    echo "Enhanced development tool with correlated client/server logging"
    echo ""
    echo "Usage: $0 [command] [options]"
    echo ""
    echo "Build Commands:"
    echo "  build         - Build firmware only"
    echo "  flash         - Flash firmware to device"
    echo "  build-flash   - Build and flash firmware"
    echo ""
    echo "Monitoring Commands:"
    echo "  monitor       - Monitor serial output only"
    echo "  monitor-reset - Monitor with device reset [duration_seconds]"
    echo "  reset         - Reset ESP32-C6 device only"
    echo "  client        - Run client simulator only [duration_seconds]"
    echo "  dual          - Dual monitoring mode (server + client) [duration_seconds]"
    echo "  logs          - Tail log files [client|server|combined]"
    echo ""
    echo "Testing Commands:"
    echo "  test          - Full hardware functionality test"
    echo "  flash-test    - Flash firmware and run quick test"
    echo ""
    echo "Utility Commands:"
    echo "  clean         - Clean build files"
    echo "  menuconfig    - Open configuration menu"
    echo "  size          - Show firmware size information"
    echo "  erase         - Erase flash completely"
    echo "  summary       - Show log summary"
    echo ""
    echo "Examples:"
    echo "  $0 dual 120              # Dual monitor for 2 minutes"
    echo "  $0 client 60             # Client simulation for 1 minute"
    echo "  $0 flash-test            # Flash and test"
    echo "  $0 logs combined         # Tail combined logs"
    echo ""
    echo "Configuration:"
    echo "  ESP-IDF Path:     $ESP_IDF_PATH"
    echo "  Firmware Dir:     $FIRMWARE_DIR"
    echo "  Device Port:      $DEVICE_PORT"
    echo "  Log Directory:    $LOG_DIR"
    echo "  Future Web IP:    $WEB_SERVER_IP:$WEB_SERVER_PORT"
}

# Function to initialize development environment
init_dev_environment() {
    print_status "Initializing ESP32-C6 development environment..."

    # Check prerequisites
    check_esp_idf
    setup_logging
    goto_firmware_dir
    setup_environment

    print_success "Development environment ready"
}

# Main execution function
main() {
    local command="${1:-dual}"
    local option="${2:-60}"

    echo ""
    print_status "========================================="
    print_status "ESP32-C6 Development Monitor v1.0"
    print_status "Correlated Client/Server Logging Tool"
    print_status "========================================="
    print_status "Command: $command"
    echo ""

    # Initialize environment for most commands
    case "$command" in
        "help"|"-h"|"--help")
            show_usage
            exit 0
            ;;
        "summary")
            setup_logging
            show_log_summary
            exit 0
            ;;
        "logs")
            setup_logging
            tail_logs "$option"
            exit 0
            ;;
        *)
            init_dev_environment
            ;;
    esac

    # Execute command
    case "$command" in
        "build")
            build_firmware
            ;;

        "flash")
            check_device
            flash_firmware
            ;;

        "build-flash")
            build_firmware
            flash_firmware
            ;;

        "monitor")
            check_device
            monitor_serial
            ;;

        "reset")
            reset_device
            ;;

        "monitor-reset")
            local duration=${2:-30}
            check_device
            monitor_with_reset "$duration"
            ;;

        "client")
            simulate_http_client "$option"
            ;;

        "dual")
            check_device
            dual_monitor "$option"
            ;;

        "test")
            test_hardware
            ;;

        "flash-test")
            build_firmware
            flash_firmware
            print_status "Running quick functionality test with device reset..."
            monitor_with_reset 15
            print_success "Quick test completed - check logs for ESP32-C6 boot output"
            ;;

        "clean")
            print_status "Cleaning build files..."
            idf.py clean
            print_success "Clean completed"
            ;;

        "menuconfig")
            print_status "Opening configuration menu..."
            idf.py menuconfig
            ;;

        "size")
            print_status "Analyzing firmware size..."
            idf.py size
            ;;

        "erase")
            check_device
            print_warning "This will erase all flash memory!"
            read -p "Are you sure? (y/N): " -n 1 -r
            echo
            if [[ $REPLY =~ ^[Yy]$ ]]; then
                print_status "Erasing flash..."
                idf.py -p "$DEVICE_PORT" erase-flash
                print_success "Flash erased"
            else
                print_status "Erase cancelled"
            fi
            ;;

        *)
            print_error "Unknown command: $command"
            echo ""
            show_usage
            exit 1
            ;;
    esac

    echo ""
    print_success "Command completed successfully"
    echo ""
}

# Cleanup function for Ctrl+C
cleanup() {
    print_warning "Received interrupt signal, cleaning up..."

    # Kill any background processes
    jobs -p | xargs -r kill 2>/dev/null || true

    print_status "Cleanup completed"
    exit 0
}

# Set up signal handlers
trap cleanup SIGINT SIGTERM

# Check if script is being sourced or executed
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi
