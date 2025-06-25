#!/bin/bash

# ESP32-C6 MCP Integration Test Script
# ===================================
#
# This script provides comprehensive testing of the ESP32-C6 MCP server
# by combining firmware monitoring with Python MCP client testing.
#
# Features:
# - Automatic ESP32-C6 build, flash, and monitoring
# - WiFi IP address extraction from serial output
# - Python MCP client testing via TCP/IP
# - Correlated logging of server and client activities
# - Comprehensive test coverage of all MCP tools
#
# Usage:
#   ./test_mcp_integration.sh [test_duration] [build_first]
#
# Examples:
#   ./test_mcp_integration.sh 120              # 2-minute test, build first
#   ./test_mcp_integration.sh 300 nobuild     # 5-minute test, skip build
#   ./test_mcp_integration.sh 60 clean        # Clean build + 1-minute test

set -e  # Exit on any error

# Configuration
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ESP_IDF_PATH="/home/mike/esp/esp-idf"
FIRMWARE_DIR="$SCRIPT_DIR/firmware"
DEVICE_PORT="/dev/ttyACM0"
LOG_DIR="$SCRIPT_DIR/logs"
TEST_DURATION=${1:-120}  # Default 2 minutes
BUILD_MODE=${2:-build}   # build, nobuild, clean

# File paths
ESP32_LOG_FILE="$LOG_DIR/esp32_server.log"
CLIENT_LOG_FILE="$LOG_DIR/mcp_client.log"
COMBINED_LOG_FILE="$LOG_DIR/integration_test.log"
IP_EXTRACT_LOG="$LOG_DIR/ip_extraction.log"

# MCP Configuration
MCP_SERVER_PORT=8080
MCP_CLIENT_TIMEOUT=15
IP_EXTRACTION_TIMEOUT=45

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

print_header() {
    echo -e "${WHITE}================================================================${NC}"
    echo -e "${WHITE}[$(timestamp)] $1${NC}"
    echo -e "${WHITE}================================================================${NC}"
}

print_status() {
    echo -e "${BLUE}[$(timestamp)] [MCP-TEST]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[$(timestamp)] [SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[$(timestamp)] [WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[$(timestamp)] [ERROR]${NC} $1"
}

print_info() {
    echo -e "${CYAN}[$(timestamp)] [INFO]${NC} $1"
}

# Function to cleanup processes
cleanup() {
    print_status "Cleaning up processes..."

    # Kill ESP32 monitor process
    if [[ -n $ESP32_MONITOR_PID ]]; then
        kill $ESP32_MONITOR_PID 2>/dev/null || true
        wait $ESP32_MONITOR_PID 2>/dev/null || true
        print_status "ESP32 monitor process stopped"
    fi

    # Kill MCP client process
    if [[ -n $MCP_CLIENT_PID ]]; then
        kill $MCP_CLIENT_PID 2>/dev/null || true
        wait $MCP_CLIENT_PID 2>/dev/null || true
        print_status "MCP client process stopped"
    fi

    # Kill IP extraction process
    if [[ -n $IP_EXTRACT_PID ]]; then
        kill $IP_EXTRACT_PID 2>/dev/null || true
        wait $IP_EXTRACT_PID 2>/dev/null || true
    fi

    print_status "Cleanup completed"
}

# Set trap for cleanup
trap cleanup EXIT INT TERM

# Function to setup environment
setup_environment() {
    print_header "SETTING UP ENVIRONMENT"

    # Create log directory
    mkdir -p "$LOG_DIR"

    # Clear previous logs
    > "$ESP32_LOG_FILE"
    > "$CLIENT_LOG_FILE"
    > "$COMBINED_LOG_FILE"
    > "$IP_EXTRACT_LOG"

    # Setup ESP-IDF environment
    if [[ -f "$ESP_IDF_PATH/export.sh" ]]; then
        print_status "Setting up ESP-IDF environment..."
        export IDF_PATH="$ESP_IDF_PATH"
        source "$ESP_IDF_PATH/export.sh" > /dev/null 2>&1
        print_success "ESP-IDF environment configured"
    else
        print_error "ESP-IDF not found at $ESP_IDF_PATH"
        exit 1
    fi

    # Check if device is connected
    if [[ ! -e "$DEVICE_PORT" ]]; then
        print_error "ESP32-C6 device not found at $DEVICE_PORT"
        print_info "Please connect the ESP32-C6 device and try again"
        exit 1
    fi

    print_success "Environment setup completed"
}

# Function to build firmware
build_firmware() {
    print_header "BUILDING ESP32-C6 FIRMWARE"

    cd "$FIRMWARE_DIR"

    case "$BUILD_MODE" in
        "clean")
            print_status "Performing clean build..."
            idf.py fullclean build
            ;;
        "build")
            print_status "Building firmware..."
            idf.py build
            ;;
        "nobuild")
            print_status "Skipping build as requested"
            return 0
            ;;
        *)
            print_status "Building firmware (default)..."
            idf.py build
            ;;
    esac

    if [[ $? -eq 0 ]]; then
        print_success "Firmware build completed successfully"
    else
        print_error "Firmware build failed"
        exit 1
    fi

    cd "$SCRIPT_DIR"
}

# Function to flash firmware
flash_firmware() {
    print_header "FLASHING ESP32-C6 FIRMWARE"

    cd "$FIRMWARE_DIR"

    print_status "Flashing firmware to $DEVICE_PORT..."
    idf.py -p "$DEVICE_PORT" flash

    if [[ $? -eq 0 ]]; then
        print_success "Firmware flashed successfully"
    else
        print_error "Firmware flash failed"
        exit 1
    fi

    cd "$SCRIPT_DIR"
}

# Function to start ESP32 monitoring
start_esp32_monitor() {
    print_header "STARTING ESP32-C6 MONITORING"

    print_status "Starting ESP32-C6 serial monitor..."

    # Start monitoring in background, logging to file
    cd "$FIRMWARE_DIR"
    idf.py -p "$DEVICE_PORT" monitor 2>&1 | tee "$ESP32_LOG_FILE" &
    ESP32_MONITOR_PID=$!
    cd "$SCRIPT_DIR"

    print_success "ESP32-C6 monitor started (PID: $ESP32_MONITOR_PID)"

    # Wait a few seconds for boot messages
    sleep 5
}

# Function to extract ESP32 IP address
extract_esp32_ip() {
    print_header "EXTRACTING ESP32-C6 IP ADDRESS"

    print_status "Waiting for ESP32-C6 to connect to WiFi and report IP address..."
    print_info "Timeout: $IP_EXTRACTION_TIMEOUT seconds"

    # Start IP extraction in background
    python3 "$SCRIPT_DIR/get_esp32_ip.py" \
        --serial-port "$DEVICE_PORT" \
        --timeout "$IP_EXTRACTION_TIMEOUT" \
        --quiet > "$IP_EXTRACT_LOG" 2>&1 &
    IP_EXTRACT_PID=$!

    # Wait for IP extraction with progress indicator
    local count=0
    while kill -0 $IP_EXTRACT_PID 2>/dev/null; do
        if [[ $count -ge $IP_EXTRACTION_TIMEOUT ]]; then
            print_error "IP extraction timeout after $IP_EXTRACTION_TIMEOUT seconds"
            kill $IP_EXTRACT_PID 2>/dev/null || true
            return 1
        fi

        printf "\r${CYAN}[$(timestamp)] [INFO]${NC} Waiting for IP address... %ds" $count
        sleep 1
        ((count++))
    done
    printf "\n"

    # Get the result
    wait $IP_EXTRACT_PID
    local exit_code=$?

    if [[ $exit_code -eq 0 ]]; then
        ESP32_IP=$(cat "$IP_EXTRACT_LOG" | tail -1)
        if [[ -n "$ESP32_IP" ]]; then
            print_success "ESP32-C6 IP address found: $ESP32_IP"
            return 0
        else
            print_error "IP extraction succeeded but no IP found in output"
            return 1
        fi
    else
        print_error "IP extraction failed (exit code: $exit_code)"
        return 1
    fi
}

# Function to wait for MCP server
wait_for_mcp_server() {
    print_header "WAITING FOR MCP SERVER"

    print_status "Checking if MCP server is ready at $ESP32_IP:$MCP_SERVER_PORT..."

    local max_attempts=30
    local attempt=1

    while [[ $attempt -le $max_attempts ]]; do
        if timeout 3 bash -c "echo > /dev/tcp/$ESP32_IP/$MCP_SERVER_PORT" 2>/dev/null; then
            print_success "MCP server is ready and accepting connections"
            return 0
        fi

        print_info "Attempt $attempt/$max_attempts - MCP server not ready yet..."
        sleep 2
        ((attempt++))
    done

    print_error "MCP server did not become ready within $((max_attempts * 2)) seconds"
    return 1
}

# Function to start MCP client testing
start_mcp_client() {
    print_header "STARTING MCP CLIENT TESTING"

    print_status "Starting Python MCP client..."
    print_info "Target: $ESP32_IP:$MCP_SERVER_PORT"
    print_info "Duration: $TEST_DURATION seconds"

    # Start MCP client in background
    python3 "$SCRIPT_DIR/mcp_client.py" \
        "$ESP32_IP" \
        "$MCP_SERVER_PORT" \
        "$TEST_DURATION" \
        --timeout "$MCP_CLIENT_TIMEOUT" \
        --verbose 2>&1 | tee "$CLIENT_LOG_FILE" &
    MCP_CLIENT_PID=$!

    print_success "MCP client started (PID: $MCP_CLIENT_PID)"
}

# Function to monitor test progress
monitor_test_progress() {
    print_header "MONITORING TEST PROGRESS"

    print_status "Test running for $TEST_DURATION seconds..."
    print_info "Monitor logs:"
    print_info "  ESP32 Server: tail -f $ESP32_LOG_FILE"
    print_info "  MCP Client:   tail -f $CLIENT_LOG_FILE"
    print_info "  Combined:     tail -f $COMBINED_LOG_FILE"

    local start_time=$(date +%s)

    # Monitor both processes
    while true; do
        local current_time=$(date +%s)
        local elapsed=$((current_time - start_time))

        # Check if test duration has elapsed
        if [[ $elapsed -ge $TEST_DURATION ]]; then
            print_status "Test duration completed ($TEST_DURATION seconds)"
            break
        fi

        # Check if MCP client is still running
        if ! kill -0 $MCP_CLIENT_PID 2>/dev/null; then
            print_status "MCP client process has terminated"
            break
        fi

        # Show progress
        local remaining=$((TEST_DURATION - elapsed))
        printf "\r${CYAN}[$(timestamp)] [PROGRESS]${NC} Running... %ds elapsed, %ds remaining" $elapsed $remaining

        sleep 5
    done
    printf "\n"
}

# Function to collect and analyze results
analyze_results() {
    print_header "ANALYZING TEST RESULTS"

    # Wait for MCP client to finish if still running
    if kill -0 $MCP_CLIENT_PID 2>/dev/null; then
        print_status "Waiting for MCP client to finish..."
        wait $MCP_CLIENT_PID
        MCP_CLIENT_EXIT_CODE=$?
    else
        MCP_CLIENT_EXIT_CODE=0
    fi

    # Combine logs with timestamps
    print_status "Combining server and client logs..."
    {
        echo "=== ESP32-C6 MCP Integration Test Results ==="
        echo "Date: $(date)"
        echo "Duration: $TEST_DURATION seconds"
        echo "ESP32 IP: $ESP32_IP"
        echo "MCP Port: $MCP_SERVER_PORT"
        echo ""
        echo "=== SERVER LOG ==="
        cat "$ESP32_LOG_FILE"
        echo ""
        echo "=== CLIENT LOG ==="
        cat "$CLIENT_LOG_FILE"
    } > "$COMBINED_LOG_FILE"

    # Analyze results
    print_status "Analyzing test results..."

    # Count successes and failures from client log
    local tests_passed=$(grep -c "test passed" "$CLIENT_LOG_FILE" 2>/dev/null || echo "0")
    local tests_failed=$(grep -c "test failed" "$CLIENT_LOG_FILE" 2>/dev/null || echo "0")
    local tools_executed=$(grep -c "tool executed" "$CLIENT_LOG_FILE" 2>/dev/null || echo "0")
    local messages_sent=$(grep -c "Messages sent:" "$CLIENT_LOG_FILE" 2>/dev/null || echo "0")
    local errors=$(grep -c "ERROR" "$CLIENT_LOG_FILE" 2>/dev/null || echo "0")

    # Check if server reported MCP activity
    local server_mcp_activity=$(grep -c "MCP" "$ESP32_LOG_FILE" 2>/dev/null || echo "0")

    print_header "TEST RESULTS SUMMARY"
    print_info "MCP Client Exit Code: $MCP_CLIENT_EXIT_CODE"
    print_info "Tests Passed: $tests_passed"
    print_info "Tests Failed: $tests_failed"
    print_info "Tools Executed: $tools_executed"
    print_info "Client Errors: $errors"
    print_info "Server MCP Activity: $server_mcp_activity messages"

    if [[ $MCP_CLIENT_EXIT_CODE -eq 0 ]] && [[ $tests_failed -eq 0 ]] && [[ $tests_passed -gt 0 ]]; then
        print_success "🎉 INTEGRATION TEST PASSED! 🎉"
        print_success "All MCP tools tested successfully"
        return 0
    else
        print_error "❌ INTEGRATION TEST FAILED ❌"
        if [[ $tests_passed -eq 0 ]]; then
            print_error "No tests passed - check connectivity and server status"
        fi
        if [[ $tests_failed -gt 0 ]]; then
            print_error "$tests_failed tests failed - check server implementation"
        fi
        if [[ $server_mcp_activity -eq 0 ]]; then
            print_error "No MCP activity detected on server - check transport layer"
        fi
        return 1
    fi
}

# Function to show help
show_help() {
    cat << EOF
ESP32-C6 MCP Integration Test Script
===================================

This script provides comprehensive testing of the ESP32-C6 MCP server
by combining firmware monitoring with Python MCP client testing.

USAGE:
    $0 [test_duration] [build_mode]

PARAMETERS:
    test_duration   Test duration in seconds (default: 120)
    build_mode     Build mode: build, nobuild, clean (default: build)

EXAMPLES:
    $0                    # 2-minute test with build
    $0 300               # 5-minute test with build
    $0 60 nobuild        # 1-minute test, skip build
    $0 120 clean         # Clean build + 2-minute test

WHAT IT DOES:
    1. Sets up ESP-IDF environment
    2. Builds ESP32-C6 firmware (unless nobuild)
    3. Flashes firmware to device
    4. Starts serial monitoring
    5. Extracts WiFi IP address from serial output
    6. Waits for MCP server to become ready
    7. Runs Python MCP client tests via TCP/IP
    8. Monitors and logs both server and client
    9. Analyzes results and reports success/failure

LOG FILES:
    logs/esp32_server.log      - ESP32-C6 serial output
    logs/mcp_client.log        - Python MCP client output
    logs/integration_test.log  - Combined results

REQUIREMENTS:
    - ESP32-C6 connected to $DEVICE_PORT
    - ESP-IDF v5.4.1 installed at $ESP_IDF_PATH
    - Python 3 with pyserial
    - WiFi credentials in firmware/wifi-creds.txt

EOF
}

# Main function
main() {
    # Check for help flag
    if [[ "$1" == "-h" ]] || [[ "$1" == "--help" ]]; then
        show_help
        exit 0
    fi

    print_header "ESP32-C6 MCP INTEGRATION TEST"
    print_info "Test Duration: $TEST_DURATION seconds"
    print_info "Build Mode: $BUILD_MODE"
    print_info "Device Port: $DEVICE_PORT"
    print_info "MCP Server Port: $MCP_SERVER_PORT"

    # Check Python dependencies
    if ! command -v python3 >/dev/null 2>&1; then
        print_error "Python 3 is required but not installed"
        exit 1
    fi

    if ! python3 -c "import serial" >/dev/null 2>&1; then
        print_error "Python pyserial module is required"
        print_info "Install with: pip3 install pyserial"
        exit 1
    fi

    # Run test sequence
    local test_failed=0

    setup_environment || test_failed=1

    if [[ $test_failed -eq 0 ]] && [[ "$BUILD_MODE" != "nobuild" ]]; then
        build_firmware || test_failed=1
        flash_firmware || test_failed=1
    fi

    if [[ $test_failed -eq 0 ]]; then
        start_esp32_monitor || test_failed=1
        extract_esp32_ip || test_failed=1
        wait_for_mcp_server || test_failed=1
        start_mcp_client || test_failed=1
        monitor_test_progress || test_failed=1
        analyze_results || test_failed=1
    fi

    if [[ $test_failed -eq 0 ]]; then
        print_success "🚀 Integration test completed successfully!"
        print_info "Check $COMBINED_LOG_FILE for detailed results"
        exit 0
    else
        print_error "💥 Integration test failed!"
        print_info "Check log files in $LOG_DIR for debugging"
        exit 1
    fi
}

# Run main function
main "$@"
