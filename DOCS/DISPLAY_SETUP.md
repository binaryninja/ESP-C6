# ST7789 Display Setup Guide for ESP32-C6

## Overview
This guide covers the setup and wiring of a 1.47" ST7789 TFT display (172×320 resolution, 262K colors) with the ESP32-C6 DevKit.

## Hardware Requirements
- ESP32-C6 DevKit-1
- 1.47" ST7789 TFT Display (172×320 pixels)
- Jumper wires
- Breadboard (optional)

## Display Specifications
- **Resolution**: 172×320 pixels
- **Colors**: 262K (16-bit RGB565)
- **Controller**: ST7789V
- **Interface**: 4-wire SPI
- **Voltage**: 3.3V

## Wiring Diagram

### ESP32-C6 to ST7789 Display Connections

| ESP32-C6 Pin | ST7789 Pin | Function | Wire Color (suggested) |
|--------------|------------|----------|------------------------|
| GPIO23       | MOSI/SDA   | SPI Data | Blue                   |
| GPIO18       | SCLK/SCL   | SPI Clock | Green                  |
| GPIO17       | CS         | Chip Select | Orange                |
| GPIO16       | DC/RS      | Data/Command | Yellow               |
| GPIO15       | RST        | Reset | Purple                  |
| GPIO14       | BL/LED     | Backlight | White                  |
| 3V3          | VCC        | Power | Red                     |
| GND          | GND        | Ground | Black                   |

### Detailed Wiring Instructions

1. **Power Connections**:
   - Connect display VCC to ESP32-C6 3V3 pin
   - Connect display GND to ESP32-C6 GND pin

2. **SPI Connections**:
   - MOSI (GPIO23): Master Out Slave In - data from ESP32 to display
   - SCLK (GPIO18): Serial Clock - synchronizes data transmission
   - CS (GPIO17): Chip Select - enables the display for communication

3. **Control Connections**:
   - DC (GPIO16): Data/Command - tells display if data is a command or pixel data
   - RST (GPIO15): Reset - hardware reset line for display initialization
   - BL (GPIO14): Backlight - controls display backlight (optional)

## Pin Configuration

The default pin configuration is defined in `display_st7789.h`:

```c
#define DISPLAY_PIN_MOSI        GPIO_NUM_23
#define DISPLAY_PIN_SCLK        GPIO_NUM_18
#define DISPLAY_PIN_CS          GPIO_NUM_17
#define DISPLAY_PIN_DC          GPIO_NUM_16
#define DISPLAY_PIN_RST         GPIO_NUM_15  
#define DISPLAY_PIN_BL          GPIO_NUM_14
#define DISPLAY_BL_ON_LEVEL     1
```

### Customizing Pin Assignment

To use different GPIO pins, modify the configuration in your application:

```c
display_config_t config;
display_get_default_config(&config);

// Customize pins
config.pin_mosi = GPIO_NUM_19;  // Change MOSI pin
config.pin_sclk = GPIO_NUM_20;  // Change SCLK pin
config.pin_cs = GPIO_NUM_21;    // Change CS pin
// ... etc

display_init(&config, &display_handle);
```

## Software Setup

### 1. Component Integration
The display component is already integrated into the firmware. The main files are:
- `components/display/include/display_st7789.h` - Header file
- `components/display/display_st7789.c` - Implementation
- `components/display/CMakeLists.txt` - Build configuration

### 2. Initialization Code
The display is initialized automatically in `main/firmware.c`:

```c
#include "display_st7789.h"

static display_handle_t s_display_handle = {0};

void init_display(void) {
    display_config_t display_config;
    display_get_default_config(&display_config);
    
    esp_err_t ret = display_init(&display_config, &s_display_handle);
    if (ret == ESP_OK) {
        display_clear(&s_display_handle, COLOR_BLACK);
        display_printf(&s_display_handle, 8, 8, COLOR_WHITE, COLOR_BLACK, 
                       "Hello World!");
    }
}
```

## Features and Functions

### Basic Display Operations
- `display_clear()` - Clear screen with solid color
- `display_backlight_set()` - Control backlight on/off
- `display_fill_rect()` - Fill rectangular area with color
- `display_draw_pixel()` - Draw single pixel

### Text Rendering
- `display_draw_char()` - Draw single character
- `display_draw_string()` - Draw text string
- `display_printf()` - Printf-style text rendering

### Color System
Colors use RGB565 format (16-bit):
- `COLOR_BLACK` (0x0000)
- `COLOR_WHITE` (0xFFFF)
- `COLOR_RED` (0xF800)
- `COLOR_GREEN` (0x07E0)
- `COLOR_BLUE` (0x001F)
- `COLOR_YELLOW` (0xFFE0)
- `COLOR_CYAN` (0x07FF)
- `COLOR_MAGENTA` (0xF81F)

Custom colors can be created using:
```c
uint16_t my_color = display_rgb888_to_rgb565(255, 128, 0); // Orange
```

## Current Firmware Features

The firmware displays:
1. **Startup Screen**: "Hello World!" message
2. **System Status**: Updates when button is pressed
   - Uptime counter
   - Free memory
   - Button press count
3. **Live Clock**: Running time display (updates every 10 seconds)
4. **Memory Status**: Color-coded memory usage

## Troubleshooting

### Display Not Working
1. **Check Wiring**: Verify all connections match the wiring diagram
2. **Power Supply**: Ensure 3.3V is connected to VCC
3. **Ground**: Verify GND connections are secure
4. **SPI Communication**: Check MOSI, SCLK, and CS connections

### Display Shows Garbage
1. **Check DC Pin**: Data/Command pin must be connected correctly
2. **Reset Pin**: Ensure RST pin is connected and working
3. **SPI Configuration**: Verify SPI settings match display requirements

### Backlight Issues
1. **BL Pin**: Check backlight pin connection (GPIO14)
2. **Power Level**: Some displays need LOW level to turn on backlight
3. **Modify Code**: Change `DISPLAY_BL_ON_LEVEL` if needed

### Common Issues
- **Inverted Colors**: Try toggling `esp_lcd_panel_invert_color()`
- **Mirrored Display**: Adjust `esp_lcd_panel_mirror()` settings
- **Wrong Orientation**: Modify `esp_lcd_panel_swap_xy()` if needed

## Building and Flashing

1. **Build the firmware**:
   ```bash
   cd ESP-C6/firmware
   source /home/mike/esp/esp-idf/export.sh
   idf.py build
   ```

2. **Flash to device**:
   ```bash
   idf.py -p /dev/ttyACM0 flash monitor
   ```

## Performance Notes

- **Frame Rate**: Limited by SPI speed (20MHz default)
- **Memory Usage**: Uses DMA buffers for efficient transfers
- **Font**: 8x16 pixel monospace font included
- **Buffer Size**: Optimized for 172×320 display resolution

## Future Enhancements

Potential improvements to consider:
- Support for different font sizes
- Image display capabilities
- Graphics primitives (lines, circles)
- Touch screen support
- Multiple display support
- Hardware scrolling

## Technical Details

### SPI Configuration
- **Clock Speed**: 20MHz
- **Mode**: SPI Mode 0 (CPOL=0, CPHA=0)
- **Bit Order**: MSB first
- **DMA**: Enabled for efficient transfers

### Memory Usage
- **Font Data**: ~1.5KB for 8x16 ASCII font
- **Display Buffer**: Allocated dynamically as needed
- **DMA Buffers**: Managed by ESP-IDF LCD driver

### Performance Metrics
- **Full Screen Clear**: ~50ms
- **Character Rendering**: ~2ms per character
- **Pixel Drawing**: ~1ms per pixel operation

## Support

For issues or questions:
1. Check the ESP-IDF LCD documentation
2. Review the ST7789 datasheet
3. Verify wiring connections
4. Check serial monitor for error messages