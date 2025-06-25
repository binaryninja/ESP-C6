#!/usr/bin/env python3
"""
MCP TCP Client for ESP32-C6 Testing

This script connects to the ESP32-C6 MCP server over TCP (port 8080) and
sends JSON-RPC commands to test the MCP tools functionality.
"""

import socket
import json
import time
import sys
import threading
from typing import Dict, Any, Optional

class MCPTCPClient:
    def __init__(self, host: str = "192.168.1.100", port: int = 8080):
        self.host = host
        self.port = port
        self.socket = None
        self.connected = False
        self.message_id = 1

    def connect(self) -> bool:
        """Connect to the ESP32-C6 MCP server"""
        try:
            print(f"Connecting to ESP32-C6 MCP server at {self.host}:{self.port}...")
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.socket.settimeout(10)  # 10 second timeout
            self.socket.connect((self.host, self.port))
            self.connected = True
            print(f"✅ Connected to ESP32-C6 MCP server!")
            return True
        except socket.error as e:
            print(f"❌ Failed to connect: {e}")
            return False

    def disconnect(self):
        """Disconnect from the ESP32-C6 MCP server"""
        if self.socket:
            self.socket.close()
            self.socket = None
        self.connected = False
        print("🔌 Disconnected from ESP32-C6 MCP server")

    def send_request(self, method: str, params: Optional[Dict[str, Any]] = None) -> Optional[Dict[str, Any]]:
        """Send a JSON-RPC request to the MCP server"""
        if not self.connected:
            print("❌ Not connected to server")
            return None

        # Build JSON-RPC request
        request = {
            "jsonrpc": "2.0",
            "method": method,
            "id": self.message_id
        }

        if params:
            request["params"] = params

        self.message_id += 1

        try:
            # Send request
            request_json = json.dumps(request) + "\n"
            print(f"📤 Sending: {request_json.strip()}")
            self.socket.send(request_json.encode('utf-8'))

            # Receive response
            response_data = self.socket.recv(4096)
            if not response_data:
                print("❌ No response received")
                return None

            response_str = response_data.decode('utf-8').strip()
            print(f"📥 Received: {response_str}")

            # Parse JSON response
            try:
                response = json.loads(response_str)
                return response
            except json.JSONDecodeError as e:
                print(f"❌ Failed to parse JSON response: {e}")
                return None

        except socket.error as e:
            print(f"❌ Socket error: {e}")
            self.connected = False
            return None

    def ping(self) -> bool:
        """Test basic connectivity with ping"""
        print("\n🏓 Testing ping...")
        response = self.send_request("ping")
        if response and "result" in response:
            print(f"✅ Ping successful: {response['result']}")
            return True
        else:
            print("❌ Ping failed")
            return False

    def list_tools(self) -> bool:
        """List available MCP tools"""
        print("\n🔧 Listing available tools...")
        response = self.send_request("tools/list")
        if response and "result" in response:
            tools = response["result"].get("tools", [])
            print(f"✅ Found {len(tools)} tools:")
            for tool in tools:
                name = tool.get("name", "Unknown")
                description = tool.get("description", "No description")
                print(f"  • {name}: {description}")
            return True
        else:
            print("❌ Failed to list tools")
            return False

    def test_echo_tool(self, message: str = "Hello ESP32-C6!") -> bool:
        """Test the echo tool"""
        print(f"\n🔄 Testing echo tool with message: '{message}'")
        response = self.send_request("tools/call", {
            "name": "echo",
            "arguments": {
                "message": message
            }
        })
        if response and "result" in response:
            print(f"✅ Echo response: {response['result']}")
            return True
        else:
            print("❌ Echo tool failed")
            return False

    def test_system_info_tool(self) -> bool:
        """Test the system info tool"""
        print("\n💻 Getting system information...")
        response = self.send_request("tools/call", {
            "name": "system_info",
            "arguments": {}
        })
        if response and "result" in response:
            print("✅ System info received:")
            result = response["result"]
            if isinstance(result, dict):
                for key, value in result.items():
                    print(f"  • {key}: {value}")
            else:
                print(f"  {result}")
            return True
        else:
            print("❌ System info tool failed")
            return False

    def test_display_control(self, text: str = "Hello from TCP!") -> bool:
        """Test the display control tool"""
        print(f"\n🖥️  Testing display control with text: '{text}'")
        response = self.send_request("tools/call", {
            "name": "display_control",
            "arguments": {
                "action": "show_text",
                "text": text,
                "x": 10,
                "y": 50
            }
        })
        if response and "result" in response:
            print(f"✅ Display control response: {response['result']}")
            return True
        else:
            print("❌ Display control failed")
            return False

    def test_gpio_control(self, led_state: bool = True) -> bool:
        """Test the GPIO control tool"""
        print(f"\n💡 Testing GPIO control - LED {'ON' if led_state else 'OFF'}")
        response = self.send_request("tools/call", {
            "name": "gpio_control",
            "arguments": {
                "action": "set_led",
                "state": led_state
            }
        })
        if response and "result" in response:
            print(f"✅ GPIO control response: {response['result']}")
            return True
        else:
            print("❌ GPIO control failed")
            return False

    def run_comprehensive_test(self):
        """Run a comprehensive test of all MCP functionality"""
        print("🚀 Starting comprehensive MCP test suite...")
        print("=" * 50)

        if not self.connect():
            return False

        tests = [
            ("Ping Test", self.ping),
            ("List Tools", self.list_tools),
            ("Echo Tool", lambda: self.test_echo_tool("TCP test message")),
            ("System Info", self.test_system_info_tool),
            ("Display Control", lambda: self.test_display_control("TCP MCP Test")),
            ("GPIO Control (LED ON)", lambda: self.test_gpio_control(True)),
            ("GPIO Control (LED OFF)", lambda: self.test_gpio_control(False)),
        ]

        passed = 0
        total = len(tests)

        for test_name, test_func in tests:
            print(f"\n{'='*20} {test_name} {'='*20}")
            try:
                if test_func():
                    passed += 1
                    print(f"✅ {test_name} PASSED")
                else:
                    print(f"❌ {test_name} FAILED")
            except Exception as e:
                print(f"❌ {test_name} FAILED with exception: {e}")

            # Small delay between tests
            time.sleep(1)

        print(f"\n{'='*50}")
        print(f"🏁 Test Results: {passed}/{total} tests passed")
        print(f"Success rate: {(passed/total)*100:.1f}%")

        self.disconnect()
        return passed == total

def auto_discover_esp32_ip() -> Optional[str]:
    """Try to automatically discover the ESP32-C6 IP address"""
    print("🔍 Attempting to auto-discover ESP32-C6 IP address...")

    # Common IP ranges for local networks
    ip_ranges = [
        "192.168.1.{}",
        "192.168.0.{}",
        "10.0.0.{}",
        "172.16.0.{}"
    ]

    for ip_pattern in ip_ranges:
        for i in range(100, 200):  # Check common DHCP range
            ip = ip_pattern.format(i)
            try:
                sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                sock.settimeout(0.1)  # Very short timeout
                result = sock.connect_ex((ip, 8080))
                sock.close()
                if result == 0:
                    print(f"✅ Found ESP32-C6 at {ip}:8080")
                    return ip
            except:
                pass

    print("❌ Could not auto-discover ESP32-C6 IP address")
    return None

def main():
    print("🔧 ESP32-C6 MCP TCP Client")
    print("=" * 30)

    # Check command line arguments
    if len(sys.argv) > 1:
        esp32_ip = sys.argv[1]
        print(f"Using IP address from command line: {esp32_ip}")
    else:
        # Try to auto-discover
        esp32_ip = auto_discover_esp32_ip()
        if not esp32_ip:
            print("\nPlease specify the ESP32-C6 IP address:")
            print("Usage: python3 mcp_tcp_client.py <ESP32_IP>")
            print("Example: python3 mcp_tcp_client.py 192.168.1.100")
            return

    # Create and run client
    client = MCPTCPClient(host=esp32_ip, port=8080)

    try:
        # Run interactive mode or comprehensive test
        if len(sys.argv) > 2 and sys.argv[2] == "--test":
            client.run_comprehensive_test()
        else:
            # Interactive mode
            print(f"\n🎮 Interactive Mode - Commands:")
            print("  connect    - Connect to ESP32-C6")
            print("  ping       - Test connectivity")
            print("  tools      - List available tools")
            print("  echo       - Test echo tool")
            print("  system     - Get system info")
            print("  display    - Test display control")
            print("  led_on     - Turn LED on")
            print("  led_off    - Turn LED off")
            print("  test       - Run comprehensive test")
            print("  quit       - Exit")

            while True:
                try:
                    cmd = input("\n> ").strip().lower()

                    if cmd == "quit":
                        break
                    elif cmd == "connect":
                        client.connect()
                    elif cmd == "ping":
                        client.ping()
                    elif cmd == "tools":
                        client.list_tools()
                    elif cmd == "echo":
                        message = input("Enter message to echo: ")
                        client.test_echo_tool(message)
                    elif cmd == "system":
                        client.test_system_info_tool()
                    elif cmd == "display":
                        text = input("Enter text to display: ")
                        client.test_display_control(text)
                    elif cmd == "led_on":
                        client.test_gpio_control(True)
                    elif cmd == "led_off":
                        client.test_gpio_control(False)
                    elif cmd == "test":
                        client.run_comprehensive_test()
                    else:
                        print("Unknown command. Type 'quit' to exit.")

                except KeyboardInterrupt:
                    print("\n\n👋 Goodbye!")
                    break
                except Exception as e:
                    print(f"Error: {e}")

            client.disconnect()

    except KeyboardInterrupt:
        print("\n\n👋 Goodbye!")
        client.disconnect()

if __name__ == "__main__":
    main()
