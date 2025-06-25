#!/usr/bin/env python3
"""
ESP32-C6 MCP Client - TCP/IP Transport
=====================================

Python client for testing the ESP32-C6 MCP (Model Context Protocol) server
via TCP/IP over WiFi. This client implements the MCP protocol to test all
available tools on the ESP32-C6 device.

Features:
- TCP/IP connection to ESP32-C6 MCP server
- JSON-RPC 2.0 protocol implementation
- Comprehensive testing of all MCP tools
- Logging and error handling
- Integration with dev_monitor.sh dual mode

Usage:
    python3 mcp_client.py <esp32_ip> [port] [test_duration]

Example:
    python3 mcp_client.py 192.168.1.100 8080 120
"""

import socket
import json
import time
import sys
import argparse
import logging
import threading
from typing import Dict, Any, Optional, List
from dataclasses import dataclass
from enum import Enum


class MCPClientState(Enum):
    """MCP Client Connection States"""
    DISCONNECTED = "disconnected"
    CONNECTING = "connecting"
    CONNECTED = "connected"
    ERROR = "error"


@dataclass
class MCPMessage:
    """MCP Message Structure"""
    jsonrpc: str = "2.0"
    id: Optional[int] = None
    method: Optional[str] = None
    params: Optional[Dict[str, Any]] = None
    result: Optional[Dict[str, Any]] = None
    error: Optional[Dict[str, Any]] = None


class MCPClient:
    """MCP Client for ESP32-C6 Communication"""

    def __init__(self, host: str, port: int = 8080, timeout: int = 10):
        """Initialize MCP Client

        Args:
            host: ESP32-C6 IP address
            port: TCP port for MCP server
            timeout: Connection timeout in seconds
        """
        self.host = host
        self.port = port
        self.timeout = timeout
        self.socket = None
        self.state = MCPClientState.DISCONNECTED
        self.message_id = 1
        self.logger = self._setup_logger()
        self.stats = {
            'messages_sent': 0,
            'messages_received': 0,
            'errors': 0,
            'tools_executed': 0,
            'start_time': None,
            'connection_time': None
        }

    def _setup_logger(self) -> logging.Logger:
        """Setup logging configuration"""
        logger = logging.getLogger('mcp_client')
        logger.setLevel(logging.INFO)

        # Create console handler with formatting
        handler = logging.StreamHandler()
        formatter = logging.Formatter(
            '%(asctime)s [MCP-CLIENT] %(levelname)s: %(message)s',
            datefmt='%Y-%m-%d %H:%M:%S'
        )
        handler.setFormatter(formatter)
        logger.addHandler(handler)

        return logger

    def connect(self) -> bool:
        """Connect to ESP32-C6 MCP server

        Returns:
            True if connection successful, False otherwise
        """
        try:
            self.state = MCPClientState.CONNECTING
            self.logger.info(f"Connecting to ESP32-C6 MCP server at {self.host}:{self.port}")

            self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.socket.settimeout(self.timeout)
            self.socket.connect((self.host, self.port))

            self.state = MCPClientState.CONNECTED
            self.stats['connection_time'] = time.time()
            self.logger.info(f"Connected to ESP32-C6 MCP server successfully")

            return True

        except socket.timeout:
            self.logger.error(f"Connection timeout after {self.timeout} seconds")
            self.state = MCPClientState.ERROR
            return False
        except ConnectionRefusedError:
            self.logger.error(f"Connection refused - is MCP server running on {self.host}:{self.port}?")
            self.state = MCPClientState.ERROR
            return False
        except Exception as e:
            self.logger.error(f"Connection failed: {e}")
            self.state = MCPClientState.ERROR
            return False

    def disconnect(self):
        """Disconnect from MCP server"""
        if self.socket:
            try:
                self.socket.close()
            except:
                pass
            self.socket = None
        self.state = MCPClientState.DISCONNECTED
        self.logger.info("Disconnected from MCP server")

    def send_message(self, method: str, params: Dict[str, Any] = None) -> Optional[Dict[str, Any]]:
        """Send MCP message and wait for response

        Args:
            method: MCP method name
            params: Method parameters

        Returns:
            Response data or None on error
        """
        if self.state != MCPClientState.CONNECTED:
            self.logger.error("Not connected to MCP server")
            return None

        # Create MCP message
        message = {
            "jsonrpc": "2.0",
            "id": self.message_id,
            "method": method,
            "params": params or {}
        }

        try:
            # Send message
            message_json = json.dumps(message) + '\n'
            self.socket.send(message_json.encode('utf-8'))
            self.stats['messages_sent'] += 1
            self.message_id += 1

            self.logger.debug(f"Sent: {message_json.strip()}")

            # Receive response
            response_data = self.socket.recv(4096)
            if not response_data:
                self.logger.error("No response received")
                return None

            response_str = response_data.decode('utf-8').strip()
            self.logger.debug(f"Received: {response_str}")

            # Parse JSON response
            response = json.loads(response_str)
            self.stats['messages_received'] += 1

            # Check for errors
            if 'error' in response:
                self.logger.error(f"MCP Error: {response['error']}")
                self.stats['errors'] += 1
                return response

            return response

        except socket.timeout:
            self.logger.error("Response timeout")
            self.stats['errors'] += 1
            return None
        except json.JSONDecodeError as e:
            self.logger.error(f"JSON decode error: {e}")
            self.stats['errors'] += 1
            return None
        except Exception as e:
            self.logger.error(f"Send message error: {e}")
            self.stats['errors'] += 1
            return None

    def test_echo_tool(self) -> bool:
        """Test the echo tool

        Returns:
            True if test passed, False otherwise
        """
        self.logger.info("Testing echo tool...")

        test_data = {
            "message": "Hello from MCP client!",
            "timestamp": time.time(),
            "test_id": "echo_001"
        }

        response = self.send_message("tools/call", {
            "name": "echo",
            "arguments": test_data
        })

        if response and 'result' in response:
            self.logger.info(f"Echo tool response: {response['result']}")
            self.stats['tools_executed'] += 1
            return True
        else:
            self.logger.error("Echo tool test failed")
            return False

    def test_system_tool(self) -> bool:
        """Test the system information tool

        Returns:
            True if test passed, False otherwise
        """
        self.logger.info("Testing system tool...")

        response = self.send_message("tools/call", {
            "name": "system_info",
            "arguments": {
                "action": "get_info"
            }
        })

        if response and 'result' in response:
            result = response['result']
            self.logger.info(f"System info: {result}")

            # Log key system information
            if 'data' in result:
                data = result['data']
                self.logger.info(f"  - Chip: {data.get('chip_model', 'Unknown')}")
                self.logger.info(f"  - IDF Version: {data.get('idf_version', 'Unknown')}")
                self.logger.info(f"  - Free Heap: {data.get('free_heap', 0)} bytes")
                self.logger.info(f"  - Uptime: {data.get('uptime_ms', 0)} ms")

            self.stats['tools_executed'] += 1
            return True
        else:
            self.logger.error("System tool test failed")
            return False

    def test_gpio_tool(self) -> bool:
        """Test the GPIO control tool

        Returns:
            True if test passed, False otherwise
        """
        self.logger.info("Testing GPIO tool...")

        # Test LED control
        self.logger.info("  - Testing LED control...")

        # Turn LED on
        response = self.send_message("tools/call", {
            "name": "gpio_control",
            "arguments": {
                "action": "set_led",
                "state": True
            }
        })

        if not (response and 'result' in response):
            self.logger.error("LED on test failed")
            return False

        time.sleep(1)  # Wait 1 second

        # Turn LED off
        response = self.send_message("tools/call", {
            "name": "gpio_control",
            "arguments": {
                "action": "set_led",
                "state": False
            }
        })

        if not (response and 'result' in response):
            self.logger.error("LED off test failed")
            return False

        # Test button reading
        self.logger.info("  - Testing button reading...")
        response = self.send_message("tools/call", {
            "name": "gpio_control",
            "arguments": {
                "action": "read_button"
            }
        })

        if response and 'result' in response:
            result = response['result']
            self.logger.info(f"Button state: {result}")
            self.stats['tools_executed'] += 1
            return True
        else:
            self.logger.error("Button reading test failed")
            return False

    def test_display_tool(self) -> bool:
        """Test the display control tool

        Returns:
            True if test passed, False otherwise
        """
        self.logger.info("Testing display tool...")

        # Get display info
        response = self.send_message("tools/call", {
            "name": "display_control",
            "arguments": {
                "action": "get_info"
            }
        })

        if not (response and 'result' in response):
            self.logger.error("Display info test failed")
            return False

        self.logger.info(f"Display info: {response['result']}")

        # Test text display
        test_text = f"MCP Client Test {int(time.time())}"
        response = self.send_message("tools/call", {
            "name": "display_control",
            "arguments": {
                "action": "show_text",
                "text": test_text,
                "x": 10,
                "y": 50
            }
        })

        if response and 'result' in response:
            self.logger.info(f"Display text result: {response['result']}")
            self.stats['tools_executed'] += 1
            return True
        else:
            self.logger.error("Display text test failed")
            return False

    def run_comprehensive_test(self, duration: int = 60) -> bool:
        """Run comprehensive test suite

        Args:
            duration: Test duration in seconds

        Returns:
            True if all tests passed, False otherwise
        """
        self.logger.info(f"Starting comprehensive MCP test suite (duration: {duration}s)")
        self.stats['start_time'] = time.time()

        start_time = time.time()
        test_interval = 10  # Run tests every 10 seconds
        next_test_time = start_time

        all_tests_passed = True

        while (time.time() - start_time) < duration:
            current_time = time.time()

            if current_time >= next_test_time:
                self.logger.info(f"Running test cycle at {current_time - start_time:.1f}s")

                # Run all tool tests
                tests = [
                    ("Echo", self.test_echo_tool),
                    ("System", self.test_system_tool),
                    ("GPIO", self.test_gpio_tool),
                    ("Display", self.test_display_tool)
                ]

                for test_name, test_func in tests:
                    try:
                        if not test_func():
                            self.logger.error(f"{test_name} test failed!")
                            all_tests_passed = False
                        else:
                            self.logger.info(f"{test_name} test passed")
                    except Exception as e:
                        self.logger.error(f"{test_name} test exception: {e}")
                        all_tests_passed = False

                next_test_time = current_time + test_interval

            time.sleep(1)  # Sleep 1 second between checks

        return all_tests_passed

    def print_statistics(self):
        """Print client statistics"""
        if self.stats['start_time']:
            duration = time.time() - self.stats['start_time']
        else:
            duration = 0

        self.logger.info("=== MCP Client Statistics ===")
        self.logger.info(f"  Duration: {duration:.1f} seconds")
        self.logger.info(f"  Messages sent: {self.stats['messages_sent']}")
        self.logger.info(f"  Messages received: {self.stats['messages_received']}")
        self.logger.info(f"  Tools executed: {self.stats['tools_executed']}")
        self.logger.info(f"  Errors: {self.stats['errors']}")

        if duration > 0:
            self.logger.info(f"  Message rate: {self.stats['messages_sent'] / duration:.2f} msg/s")

        if self.stats['messages_sent'] > 0:
            success_rate = ((self.stats['messages_sent'] - self.stats['errors']) / self.stats['messages_sent']) * 100
            self.logger.info(f"  Success rate: {success_rate:.1f}%")


def main():
    """Main function"""
    parser = argparse.ArgumentParser(
        description="ESP32-C6 MCP Client - TCP/IP Transport",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python3 mcp_client.py 192.168.1.100        # Default port 8080, 60s test
  python3 mcp_client.py 192.168.1.100 8080   # Custom port, 60s test
  python3 mcp_client.py 192.168.1.100 8080 120  # Custom port, 120s test

Integration with dev_monitor.sh:
  ./dev_monitor.sh dual 120 &
  python3 mcp_client.py $(get_esp32_ip) 8080 120
        """
    )

    parser.add_argument('host', help='ESP32-C6 IP address')
    parser.add_argument('port', type=int, nargs='?', default=8080,
                       help='TCP port (default: 8080)')
    parser.add_argument('duration', type=int, nargs='?', default=60,
                       help='Test duration in seconds (default: 60)')
    parser.add_argument('--timeout', type=int, default=10,
                       help='Connection timeout in seconds (default: 10)')
    parser.add_argument('--verbose', '-v', action='store_true',
                       help='Enable verbose logging')

    args = parser.parse_args()

    # Configure logging level
    if args.verbose:
        logging.getLogger('mcp_client').setLevel(logging.DEBUG)

    # Create and run client
    client = MCPClient(args.host, args.port, args.timeout)

    try:
        # Connect to server
        if not client.connect():
            print(f"Failed to connect to ESP32-C6 at {args.host}:{args.port}")
            return 1

        # Run comprehensive test
        success = client.run_comprehensive_test(args.duration)

        # Print results
        client.print_statistics()

        if success:
            client.logger.info("All tests completed successfully!")
            return 0
        else:
            client.logger.error("Some tests failed!")
            return 1

    except KeyboardInterrupt:
        client.logger.info("Test interrupted by user")
        return 1
    except Exception as e:
        client.logger.error(f"Unexpected error: {e}")
        return 1
    finally:
        client.disconnect()


if __name__ == "__main__":
    sys.exit(main())
