# TCS3472 Color Sensor Documentation

A MicroPython library for the TCS3472 RGB color and light sensing chip. This sensor provides precise color measurements and ambient light detection.

## Quick API Reference

### Core Methods
- `raw()` - Get raw CRGB values (clear, red, green, blue)
- `scaled()` - Get normalized RGB values (0.0-1.0)
- `rgb()` - Get RGB values (0-255)
- `light()` - Get ambient light level (clear channel)
- `brightness(level=65.535)` - Get brightness percentage
- `valid()` - Check if measurement is valid

## Contents

1. [Installation](#installation)
2. [Basic Usage](#basic-usage)
3. [Color Detection](#color-detection)
4. [Light Sensing](#light-sensing)
5. [API Reference](#api-reference)
6. [Examples](#examples)

## Installation

Copy the `tcs3472.py` file to your MicroPython device.

## Basic Usage

### Initialize the Sensor

```python
from machine import I2C, Pin
from tcs3472 import tcs3472

# Create I2C object
i2c = I2C(0, scl=Pin(22), sda=Pin(21))

# Create TCS3472 instance
sensor = tcs3472(i2c)  # Uses default I2C address (0x29)
```

### Read Color Values

```python
# Get RGB values (0-255)
red, green, blue = sensor.rgb()
print(f"RGB: ({red}, {green}, {blue})")

# Get normalized values (0.0-1.0)
r, g, b = sensor.scaled()
print(f"Scaled: ({r:.3f}, {g:.3f}, {b:.3f})")

# Get raw sensor values
clear, red, green, blue = sensor.raw()
print(f"Raw CRGB: ({clear}, {red}, {green}, {blue})")
```

### Basic Light Detection

```python
# Get ambient light level
light_level = sensor.light()
print(f"Light level: {light_level}")

# Get brightness percentage
brightness = sensor.brightness()
print(f"Brightness: {brightness}%")
```

## Color Detection

The TCS3472 can detect and classify colors based on their RGB values.

### Color Classification Example

```python
import time
from machine import I2C, Pin
from tcs3472 import tcs3472

# Setup
i2c = I2C(0, scl=Pin(22), sda=Pin(21))
sensor = tcs3472(i2c)

def classify_color(r, g, b):
    """Classify color based on RGB values"""
    # Normalize to prevent brightness affecting classification
    total = r + g + b
    if total == 0:
        return "black"
    
    r_norm = r / total
    g_norm = g / total
    b_norm = b / total
    
    # Color classification thresholds
    if r_norm > 0.4 and g_norm < 0.3 and b_norm < 0.3:
        return "red"
    elif g_norm > 0.4 and r_norm < 0.3 and b_norm < 0.3:
        return "green"
    elif b_norm > 0.4 and r_norm < 0.3 and g_norm < 0.3:
        return "blue"
    elif r_norm > 0.3 and g_norm > 0.3 and b_norm < 0.2:
        return "yellow"
    elif r_norm > 0.3 and g_norm < 0.2 and b_norm > 0.3:
        return "magenta"
    elif r_norm < 0.2 and g_norm > 0.3 and b_norm > 0.3:
        return "cyan"
    elif r_norm > 0.8 and g_norm > 0.8 and b_norm > 0.8:
        return "white"
    else:
        return "unknown"

# Main loop
while True:
    if sensor.valid():
        r, g, b = sensor.rgb()
        color = classify_color(r, g, b)
        print(f"Color: {color} - RGB: ({r}, {g}, {b})")
    else:
        print("Waiting for valid reading...")
    
    time.sleep(0.5)
```

## Light Sensing

The sensor can measure ambient light levels and detect lighting conditions.

### Light Level Monitoring

```python
import time
from machine import I2C, Pin
from tcs3472 import tcs3472

# Setup
i2c = I2C(0, scl=Pin(22), sda=Pin(21))
sensor = tcs3472(i2c)

def light_condition(light_level):
    """Classify lighting conditions"""
    if light_level < 100:
        return "dark"
    elif light_level < 1000:
        return "dim"
    elif light_level < 10000:
        return "normal"
    else:
        return "bright"

# Monitor light levels
while True:
    light = sensor.light()
    condition = light_condition(light)
    brightness = sensor.brightness()
    
    print(f"Light: {light} ({condition}) - Brightness: {brightness}%")
    time.sleep(1)
```

## API Reference

### Class: tcs3472

#### Constructor
```python
tcs3472(bus, address=0x29)
```
- `bus`: I2C bus object (machine.I2C)
- `address`: I2C address of the sensor (default: 0x29)

#### Core Methods

**raw()**
- Get raw sensor values
- Returns: Tuple (clear, red, green, blue) - 16-bit values

**scaled()**
- Get normalized RGB values
- Returns: Tuple (r, g, b) - Values scaled 0.0-1.0 relative to clear channel
- Returns: (0, 0, 0) if clear channel is 0

**rgb()**
- Get RGB values in standard 0-255 range
- Returns: Tuple (r, g, b) - Integer values 0-255

**light()**
- Get ambient light level
- Returns: Clear channel value (ambient light intensity)

**brightness(level=65.535)**
- Get brightness as percentage
- `level`: Maximum expected light level (default: 65.535)
- Returns: Integer percentage (0-100)

**valid()**
- Check if current measurement is valid
- Returns: Boolean - True if measurement is ready

## Examples

### Example: Simple Color Reader

```python
from machine import I2C, Pin
from tcs3472 import tcs3472
import time

# Setup
i2c = I2C(0, scl=Pin(22), sda=Pin(21))
sensor = tcs3472(i2c)

print("TCS3472 Color Reader")
print("Place objects near sensor...")

while True:
    if sensor.valid():
        # Get different formats
        clear, red, green, blue = sensor.raw()
        r_scaled, g_scaled, b_scaled = sensor.scaled()
        r_rgb, g_rgb, b_rgb = sensor.rgb()
        light_level = sensor.light()
        
        print(f"Raw CRGB: ({clear}, {red}, {green}, {blue})")
        print(f"Scaled RGB: ({r_scaled:.3f}, {g_scaled:.3f}, {b_scaled:.3f})")
        print(f"RGB (0-255): ({r_rgb}, {g_rgb}, {b_rgb})")
        print(f"Light level: {light_level}")
        print("-" * 50)
    else:
        print("Waiting for valid reading...")
    
    time.sleep(1)
```

## Notes

- **Sensor Orientation**: The sensor should face the object being measured
- **Distance**: Optimal measurement distance is 3-10mm from the object
- **Lighting**: Ambient light affects readings; consider using the sensor's built-in LED
- **Calibration**: Color classification may require calibration for your specific application
- **I2C Address**: Default address is 0x29, not user-configurable on most breakout boards

## Troubleshooting

**No valid readings**: Check I2C connections, verify sensor power, ensure correct address
**Inconsistent colors**: Maintain consistent distance and lighting conditions
**Dark readings**: Ensure adequate lighting or consider external illumination
**I2C errors**: Verify wiring, check pull-up resistors, ensure stable power supply
