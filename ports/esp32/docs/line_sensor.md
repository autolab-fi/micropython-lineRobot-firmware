# Octoliner Library Documentation

A MicroPython library for the Octoliner line following sensor - an 8-channel infrared sensor array designed for line tracking applications.

## Quick API Reference

### Core Methods
- `begin(i2c=None)` - Initialize the sensor
- `analog_read(sensor)` - Read raw analog value from sensor (0-7)
- `analog_read_all()` - Read all 8 sensors at once
- `track_line()` - Get line position (-1.0 to 1.0)
- `digital_read_all()` - Get 8-bit pattern representing line detection
- `set_sensitivity(value)` - Set sensor sensitivity (0-255)
- `optimize_sensitivity_on_black()` - Auto-calibrate for black surfaces

### Utility Methods
- `count_of_black()` - Count sensors detecting black
- `get_sensitivity()` - Get current sensitivity value
- `change_addr(new_addr)` - Change I2C address
- `save_addr()` - Save I2C address to flash

## Contents

1. [Installation](#installation)
2. [Basic Usage](#basic-usage)
3. [Line Tracking](#line-tracking)
4. [Sensor Calibration](#sensor-calibration)
5. [Advanced Features](#advanced-features)
6. [API Reference](#api-reference)
7. [Examples](#examples)

## Installation

Copy the following files to your MicroPython device:
- `octoliner.py` - Main library
- `gpio_expander.py` - GPIO expander base class
- `i2cio_commands.py` - I2C communication commands

## Basic Usage

### Initialize the Sensor

```python
from machine import I2C, Pin
from octoliner import Octoliner

# Create I2C object
i2c = I2C(0, scl=Pin(22), sda=Pin(21))

# Create Octoliner instance
sensor = Octoliner()  # Uses default I2C address (42)
sensor.begin(i2c)
```

### Read Individual Sensors

```python
# Read analog value from sensor 0 (leftmost)
value = sensor.analog_read(0)
print(f"Sensor 0: {value}")

# Read all sensors at once
all_values = sensor.analog_read_all()
print(f"All sensors: {all_values}")
```

### Basic Line Detection

```python
# Get digital pattern (8-bit value)
pattern = sensor.digital_read_all()
print(f"Pattern: {bin(pattern)}")

# Get line position
position = sensor.track_line()
print(f"Line position: {position}")
```

## Line Tracking

The `track_line()` method is the core function for line following applications.

### Understanding Line Position

- **Range**: -1.0 (far left) to 1.0 (far right)
- **Center**: 0.0 (line directly under center sensors)
- **No Line**: Returns last valid position (prevents sudden movements)

### Line Tracking Example

```python
import time
from machine import I2C, Pin
from octoliner import Octoliner

# Setup
i2c = I2C(0, scl=Pin(22), sda=Pin(21))
sensor = Octoliner()
sensor.begin(i2c)

# Auto-calibrate for black line
if sensor.optimize_sensitivity_on_black():
    print("Calibration successful!")
else:
    print("Calibration failed, using default sensitivity")

# Main tracking loop
while True:
    position = sensor.track_line()
    
    if not math.isnan(position):
        print(f"Line at position: {position:+.2f}")
        
        # Simple control logic
        if position < -0.1:
            print("Turn left")
        elif position > 0.1:
            print("Turn right")
        else:
            print("Go straight")
    else:
        print("No line detected")
    
    time.sleep(0.1)
```

## Sensor Calibration

### Manual Sensitivity Setting

```python
# Set sensitivity (0-255)
sensor.set_sensitivity(180)  # Higher = more sensitive
```

### Automatic Calibration

```python
# Place sensor over black surface and run
success = sensor.optimize_sensitivity_on_black()
if success:
    print(f"Optimized sensitivity: {sensor.get_sensitivity()}")
else:
    print("Calibration failed")
```

### Calibration Process

The automatic calibration:
1. Starts at maximum sensitivity (255)
2. Reduces sensitivity until all sensors detect black
3. Fine-tunes to find optimal threshold
4. Sets sensitivity 5 steps back for reliability

## Advanced Features

### Multiple Octoliner Sensors

```python
# Use different I2C addresses
sensor1 = Octoliner(42)  # Default address
sensor2 = Octoliner(43)  # Custom address

# Change address (requires device restart)
sensor1.change_addr(50)
sensor1.save_addr()  # Save to flash memory
```

### Custom Line Detection

```python
# Get raw analog values
values = sensor.analog_read_all()

# Convert to pattern manually
pattern = sensor.map_analog_to_pattern(values)

# Convert pattern to position
position = sensor.map_pattern_to_line(pattern)
```

### Black Surface Detection

```python
# Count sensors detecting black
black_count = sensor.count_of_black()
print(f"Sensors on black: {black_count}")

# Detect intersections or wide lines
if black_count >= 6:
    print("Wide line or intersection detected")
```

## API Reference

### Class: Octoliner

#### Constructor
```python
Octoliner(i2c_address=42)
```
- `i2c_address`: I2C address of the device (default: 42)

#### Core Methods

**begin(i2c=None)**
- Initialize the sensor with I2C object
- `i2c`: machine.I2C object

**analog_read(sensor)**
- Read raw analog value from specified sensor
- `sensor`: Sensor index (0-7, where 0 is leftmost)
- Returns: Analog value (0-1023)

**analog_read_all()**
- Read all sensors simultaneously
- Returns: List of 8 analog values

**track_line(arg=None)**
- Main line tracking function
- `arg`: None (read sensors), int (pattern), or list (analog values)
- Returns: Line position (-1.0 to 1.0) or NaN if no line

**digital_read_all()**
- Get binary pattern representing line detection
- Returns: 8-bit integer pattern

#### Calibration Methods

**set_sensitivity(sense)**
- Set sensor sensitivity
- `sense`: Sensitivity value (0-255)

**get_sensitivity()**
- Get current sensitivity
- Returns: Current sensitivity value

**optimize_sensitivity_on_black()**
- Auto-calibrate for black surfaces
- Returns: True if successful, False otherwise

#### Utility Methods

**count_of_black()**
- Count sensors detecting black
- Returns: Number of sensors on black (0-8)

**map_analog_to_pattern(analog_values)**
- Convert analog readings to binary pattern
- `analog_values`: List of 8 analog values
- Returns: 8-bit pattern

**map_pattern_to_line(binary_line)**
- Convert binary pattern to line position
- `binary_line`: 8-bit pattern
- Returns: Line position or NaN

## Examples

### Example 1: Basic Line Following Robot


### Example 2: Intersection Detection

```python
from machine import I2C, Pin
from octoliner import Octoliner
import time

# Setup
i2c = I2C(0, scl=Pin(22), sda=Pin(21))
sensor = Octoliner()
sensor.begin(i2c)

def detect_intersection():
    """Detect T-junction or cross intersection"""
    black_count = sensor.count_of_black()
    
    if black_count >= 6:
        return "intersection"
    elif black_count >= 4:
        return "wide_line"
    else:
        return "normal"

# Main loop
while True:
    line_type = detect_intersection()
    position = sensor.track_line()
    
    print(f"Type: {line_type}, Position: {position:.2f}")
    
    if line_type == "intersection":
        print("Intersection detected!")
        # Add intersection handling logic here
    
    time.sleep(0.1)
```

### Example 3: Sensor Debugging

```python
from machine import I2C, Pin
from octoliner import Octoliner
import time

# Setup
i2c = I2C(0, scl=Pin(22), sda=Pin(21))
sensor = Octoliner()
sensor.begin(i2c)

def print_sensor_data():
    """Print detailed sensor information"""
    # Raw analog values
    analog_values = sensor.analog_read_all()
    print("Analog values:", analog_values)
    
    # Binary pattern
    pattern = sensor.digital_read_all()
    print(f"Pattern: {bin(pattern):>010s}")
    
    # Line position
    position = sensor.track_line()
    print(f"Position: {position:.3f}")
    
    # Black count
    black_count = sensor.count_of_black()
    print(f"Black sensors: {black_count}")
    
    # Sensitivity
    sensitivity = sensor.get_sensitivity()
    print(f"Sensitivity: {sensitivity}")
    
    print("-" * 40)

# Debug loop
while True:
    print_sensor_data()
    time.sleep(0.5)
```

## Notes

- **Sensor Layout**: 8 sensors arranged in a line, numbered 0-7 from left to right
- **Surface Requirements**: Works best on high-contrast surfaces (black line on white background)
- **Calibration**: Always calibrate for your specific surface conditions
- **I2C Address**: Default address is 42, can be changed and saved to flash
- **Power**: Requires stable power supply for consistent readings

## Troubleshooting

**No line detected**: Check calibration, ensure good contrast, verify I2C connections
**Inconsistent readings**: Calibrate sensitivity, check for ambient light interference
**I2C errors**: Verify wiring, check I2C address, ensure stable power supply
