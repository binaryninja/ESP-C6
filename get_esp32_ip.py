#!/usr/bin/env python3
"""
ESP32-C6 IP Address Extractor
============================

This script extracts the ESP32-C6 IP address from the dev_monitor.sh output
or directly from the serial port. It's designed to work with the WiFi manager
component and can be used to automatically configure the MCP client.

Usage:
    python3 get_esp32_ip.py [--serial-port /dev/ttyACM0] [--timeout 30]

Examples:
    python3 get_esp32_ip.py                    # Use default serial port
    python3 get_esp32_ip.py --timeout 60       # Wait up to 60 seconds
    ./dev_monitor.sh build flash & python3 get_esp32_ip.py  # Get IP after flash
"""

import serial
import re
import time
import sys
import argparse
import logging
from typing import Optional

class ESP32IPExtractor:
    """ESP32-C6 IP Address Extractor"""

    def __init__(self, serial_port: str = "/dev/ttyACM0", baud_rate: int = 115200, timeout: int = 30):
        """Initialize IP extractor

        Args:
            serial_port: Serial port path
            baud_rate: Serial baud rate
            timeout: Timeout in seconds
        """
        self.serial_port = serial_port
        self.baud_rate = baud_rate
        self.timeout = timeout
        self.logger = self._setup_logger()

        # IP address patterns to match
        self.ip_patterns = [
            r'sta ip:\s*(\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3})',
            r'Got IP address:\s*(\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3})',
            r'IP[:\s]+(\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3})',
            r'WiFi connected[:\s]+(\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3})',
            r'Station IP[:\s]+(\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3})',
            r'IP4 addr[:\s]+(\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3})',
            r'WIFI:(\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3})',
            r'ESP32-C6 IP[:\s]+(\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3})',
            r'MCP server.*IP[:\s]+(\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3})',
            r'HTTP server.*(\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3})',
        ]

    def _setup_logger(self) -> logging.Logger:
        """Setup logging configuration"""
        logger = logging.getLogger('esp32_ip_extractor')
        logger.setLevel(logging.INFO)

        # Create console handler with formatting
        handler = logging.StreamHandler()
        formatter = logging.Formatter(
            '%(asctime)s [ESP32-IP] %(levelname)s: %(message)s',
            datefmt='%Y-%m-%d %H:%M:%S'
        )
        handler.setFormatter(formatter)
        logger.addHandler(handler)

        return logger

    def validate_ip(self, ip: str) -> bool:
        """Validate IP address format and exclude invalid ranges

        Args:
            ip: IP address string

        Returns:
            True if valid IP address
        """
        parts = ip.split('.')
        if len(parts) != 4:
            return False

        try:
            nums = [int(part) for part in parts]
        except ValueError:
            return False

        # Check range for each octet
        for num in nums:
            if num < 0 or num > 255:
                return False

        # Exclude invalid ranges
        if nums[0] == 0 or nums[0] == 127:  # Invalid first octet
            return False
        if nums[0] == 169 and nums[1] == 254:  # Link-local
            return False
        if nums[0] >= 224:  # Multicast/reserved
            return False

        return True

    def extract_from_line(self, line: str) -> Optional[str]:
        """Extract IP address from a single line

        Args:
            line: Line of text to search

        Returns:
            IP address if found, None otherwise
        """
        for pattern in self.ip_patterns:
            match = re.search(pattern, line, re.IGNORECASE)
            if match:
                ip = match.group(1)
                if self.validate_ip(ip):
                    return ip
        return None

    def extract_from_serial(self) -> Optional[str]:
        """Extract IP address from serial port

        Returns:
            IP address if found, None if timeout
        """
        self.logger.info(f"Monitoring serial port {self.serial_port} for ESP32-C6 IP address...")
        self.logger.info(f"Timeout: {self.timeout} seconds")

        try:
            # Open serial connection
            with serial.Serial(self.serial_port, self.baud_rate, timeout=1) as ser:
                start_time = time.time()

                while (time.time() - start_time) < self.timeout:
                    try:
                        # Read line from serial port
                        line = ser.readline().decode('utf-8', errors='ignore').strip()

                        if line:
                            self.logger.debug(f"Serial: {line}")

                            # Try to extract IP from this line
                            ip = self.extract_from_line(line)
                            if ip:
                                self.logger.info(f"Found ESP32-C6 IP address: {ip}")
                                return ip

                    except UnicodeDecodeError:
                        # Skip lines with decode errors
                        continue
                    except Exception as e:
                        self.logger.warning(f"Error reading serial: {e}")
                        continue

                self.logger.error(f"Timeout after {self.timeout} seconds - no IP address found")
                return None

        except serial.SerialException as e:
            self.logger.error(f"Serial port error: {e}")
            return None
        except FileNotFoundError:
            self.logger.error(f"Serial port {self.serial_port} not found")
            return None
        except PermissionError:
            self.logger.error(f"Permission denied accessing {self.serial_port}")
            self.logger.info("Try: sudo usermod -a -G dialout $USER (then logout/login)")
            return None

    def extract_from_log_file(self, log_file: str) -> Optional[str]:
        """Extract IP address from log file

        Args:
            log_file: Path to log file

        Returns:
            IP address if found, None otherwise
        """
        self.logger.info(f"Searching for IP address in log file: {log_file}")

        try:
            with open(log_file, 'r') as f:
                for line_num, line in enumerate(f, 1):
                    line = line.strip()
                    ip = self.extract_from_line(line)
                    if ip:
                        self.logger.info(f"Found ESP32-C6 IP address: {ip} (line {line_num})")
                        return ip

            self.logger.error("No IP address found in log file")
            return None

        except FileNotFoundError:
            self.logger.error(f"Log file not found: {log_file}")
            return None
        except Exception as e:
            self.logger.error(f"Error reading log file: {e}")
            return None


def main():
    """Main function"""
    parser = argparse.ArgumentParser(
        description="ESP32-C6 IP Address Extractor",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python3 get_esp32_ip.py                      # Monitor default serial port
  python3 get_esp32_ip.py --timeout 60         # Wait up to 60 seconds
  python3 get_esp32_ip.py --log logs/server.log # Extract from log file
  python3 get_esp32_ip.py --serial-port /dev/ttyUSB0  # Custom serial port

Integration with dev_monitor.sh:
  ./dev_monitor.sh build flash &
  ESP32_IP=$(python3 get_esp32_ip.py --timeout 60)
  python3 mcp_client.py $ESP32_IP 8080 120
        """
    )

    parser.add_argument('--serial-port', '-p', default='/dev/ttyACM0',
                       help='Serial port path (default: /dev/ttyACM0)')
    parser.add_argument('--baud-rate', '-b', type=int, default=115200,
                       help='Serial baud rate (default: 115200)')
    parser.add_argument('--timeout', '-t', type=int, default=30,
                       help='Timeout in seconds (default: 30)')
    parser.add_argument('--log-file', '-l',
                       help='Extract from log file instead of serial port')
    parser.add_argument('--verbose', '-v', action='store_true',
                       help='Enable verbose logging')
    parser.add_argument('--quiet', '-q', action='store_true',
                       help='Only output IP address (for scripting)')

    args = parser.parse_args()

    # Create extractor
    extractor = ESP32IPExtractor(args.serial_port, args.baud_rate, args.timeout)

    # Configure logging
    if args.verbose:
        extractor.logger.setLevel(logging.DEBUG)
    elif args.quiet:
        extractor.logger.setLevel(logging.ERROR)

    try:
        # Extract IP address
        if args.log_file:
            ip = extractor.extract_from_log_file(args.log_file)
        else:
            ip = extractor.extract_from_serial()

        if ip:
            if args.quiet:
                print(ip)  # Only output IP for scripting
            else:
                extractor.logger.info(f"ESP32-C6 IP Address: {ip}")
            return 0
        else:
            if not args.quiet:
                extractor.logger.error("Failed to extract IP address")
            return 1

    except KeyboardInterrupt:
        if not args.quiet:
            extractor.logger.info("Interrupted by user")
        return 1
    except Exception as e:
        if not args.quiet:
            extractor.logger.error(f"Unexpected error: {e}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
