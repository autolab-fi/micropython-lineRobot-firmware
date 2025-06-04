"""
This file is a part of Octoliner library for MicroPython.

Original product page: https://amperka.ru/product/zelo-folow-line-sensor
© Amperka LLC (https://amperka.com, dev@amperka.com)

Original Authors: Vasily Basalaev <vasily@amperka.ru>, Yury Botov <by@amperka.com>
MicroPython port by: [Your Name]
License: GPLv3, all text here must be included in any redistribution.
"""

import math
from time import sleep_ms
from gpio_expander import GpioExpander
from i2cio_commands import OUTPUT, DEFAULT_GPIOEXP_ADDR

# Minimum adequate sensitivity. For all values below this, channels always
# see black even on mirror/white surfaces
MIN_SENSITIVITY = 120
# All analog values above this are considered black
BLACK_THRESHOLD = 100

class Octoliner(GpioExpander):
    # Sensor pin mapping
    _sensor_pin_map = [4, 5, 6, 8, 7, 3, 2, 1]
    # IR LEDs pin and sensitivity control pin
    _ir_leds_pin = 9
    _sense_pin = 0
    
    def __init__(self, i2c_address=DEFAULT_GPIOEXP_ADDR):
        """Initialize Octoliner with specified I2C address."""
        super().__init__(i2c_address)
        self._previous_value = 0.0
        self._sensitivity = 208
    
    def begin(self, i2c=None):
        """Initialize the Octoliner.
        
        Args:
            i2c: machine.I2C object. If None, current I2C object is used.
        """
        #if i2c:
        #     i2c.init()
        super().begin(i2c)
        self.pwm_freq(8000)  # ~ 250 pwm levels
        self.pin_mode(self._ir_leds_pin, OUTPUT)
        self.digital_write(self._ir_leds_pin, True)
    
    def set_sensitivity(self, sense):
        """Set sensitivity of the line sensors.
        
        Args:
            sense: sensitivity value (0-255)
        """
        self._sensitivity = sense
        self.analog_write(self._sense_pin, self._sensitivity)
    
    def analog_read(self, sensor):
        """Read analog value from specified sensor.
        
        Args:
            sensor: sensor index (0-7)
            
        Returns:
            Analog value
        """
        return super().analog_read(self._sensor_pin_map[sensor & 0x07])
    
    def analog_read_all(self):
        """Read analog values from all sensors.
        
        Returns:
            List of 8 analog values
        """
        analog_values = [0] * 8
        for i in range(8):
            analog_values[i] = self.analog_read(i)
        return analog_values
    
    def map_analog_to_pattern(self, analog_values):
        """Convert analog values to bit pattern.
        
        Args:
            analog_values: list of 8 analog values
            
        Returns:
            8-bit pattern representing line position
        """
        pattern = 0
        
        # Search min and max values
        min_val = min(analog_values)
        max_val = max(analog_values)
        
        # Calculate threshold level
        threshold = min_val + (max_val - min_val) // 2
        
        # Create bit pattern
        for i in range(8):
            pattern = (pattern << 1) | (1 if analog_values[i] >= threshold else 0)
            
        return pattern
    
    def map_pattern_to_line(self, binary_line):
        """Convert bit pattern to line position.
        
        Args:
            binary_line: 8-bit pattern
            
        Returns:
            Line position in range -1.0 to 1.0 or NaN if no line detected
        """
        pattern_map = {
            0b00011000: 0,
            0b00010000: 0.25,
            0b00111000: 0.25,
            0b00001000: -0.25,
            0b00011100: -0.25,
            0b00110000: 0.375,
            0b00001100: -0.375,
            0b00100000: 0.5,
            0b01110000: 0.5,
            0b00000100: -0.5,
            0b00001110: -0.5,
            0b01100000: 0.625,
            0b11100000: 0.625,
            0b00000110: -0.625,
            0b00000111: -0.625,
            0b01000000: 0.75,
            0b11110000: 0.75,
            0b00000010: -0.75,
            0b00001111: -0.75,
            0b11000000: 0.875,
            0b00000011: -0.875,
            0b10000000: 1.0,
            0b00000001: -1.0,
        }
        
        return pattern_map.get(binary_line, float('nan'))
    
    def track_line(self, arg=None):
        """Track line position from sensor data.
        
        Can be called in three ways:
        - track_line(): Reads sensors and returns position
        - track_line(pattern): Takes bit pattern and returns position
        - track_line(analog_values): Takes list of analog values and returns position
        
        Returns:
            Line position in range -1.0 to 1.0 or previous valid position
        """
        if arg is None:
            # Read all sensors
            analog_values = self.analog_read_all()
            return self.track_line(analog_values)
        elif isinstance(arg, int):
            # Treat as pattern
            result = self.map_pattern_to_line(arg)
            result = self._previous_value if math.isnan(result) else result
            self._previous_value = result
            return result
        else:
            # Treat as analog values list
            return self.track_line(self.map_analog_to_pattern(arg))
    
    def digital_read_all(self):
        """Read digital pattern from all sensors.
        
        Returns:
            8-bit pattern representing line position
        """
        analog_values = self.analog_read_all()
        return self.map_analog_to_pattern(analog_values)
    
    def count_of_black(self):
        """Count sensors that detect black.
        
        Returns:
            Number of sensors detecting black (0-8)
        """
        count = 0
        for i in range(8):
            if self.analog_read(i) > BLACK_THRESHOLD:
                count += 1
        return count
    
    def optimize_sensitivity_on_black(self):
        """Optimize sensitivity for black surfaces.
        
        Returns:
            True if optimization succeeded, False otherwise
        """
        sensitivity_backup = self.get_sensitivity()
        
        # Give more time to settle RCL circuit at first time
        self.set_sensitivity(255)
        sleep_ms(200)
        
        # Starting at the highest possible sensitivity read all channels at each iteration
        # to find the level when all the channels become black
        sens = 255
        while sens > MIN_SENSITIVITY:
            self.set_sensitivity(sens)
            # Give some time to settle RCL circuit
            sleep_ms(100)
            if self.count_of_black() == 8:
                break
            sens -= 5
        
        if sens <= MIN_SENSITIVITY:
            # Something is broken
            self.set_sensitivity(sensitivity_backup)
            return False
        
        # Forward fine search to find the level when at least one sensor value will
        # become back white
        while sens < 255:
            self.set_sensitivity(sens)
            sleep_ms(50)
            if self.count_of_black() != 8:
                break
            sens += 1
        
        if sens == 255:
            # Environment has changed since the start of the process
            self.set_sensitivity(sensitivity_backup)
            return False
        
        # Magic 5 step back to fall back to all-eight-black
        sens -= 5
        self.set_sensitivity(sens)
        return True
    
    def get_sensitivity(self):
        """Get current sensitivity value.
        
        Returns:
            Sensitivity value (0-255)
        """
        return self._sensitivity
    
    def change_addr(self, new_addr):
        """Change I2C address.
        
        Args:
            new_addr: new I2C address
        """
        super().change_addr(new_addr)
    
    def save_addr(self):
        """Save current I2C address to device flash."""
        super().save_addr()
    
    # Deprecated method
    def map_line(self, analog_values):
        """Deprecated: Use track_line instead.
        
        Args:
            analog_values: list of analog values
            
        Returns:
            Line position
        """
        return self.track_line(analog_values)
