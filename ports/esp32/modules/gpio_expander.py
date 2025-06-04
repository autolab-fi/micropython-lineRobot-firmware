"""
This file is a part of Octoliner library for MicroPython.

Original product page: https://amperka.ru/product/zelo-folow-line-sensor
© Amperka LLC (https://amperka.com, dev@amperka.com)

Original Authors: Vasily Basalaev <vasily@amperka.ru>, Yury Botov <by@amperka.com>
MicroPython port by: [Your Name]
License: GPLv3, all text here must be included in any redistribution.
"""

import machine
from time import sleep_ms
from i2cio_commands import IOcommand, INPUT, OUTPUT, INPUT_PULLUP, INPUT_PULLDOWN, DEFAULT_GPIOEXP_ADDR

class GpioExpander:
    def __init__(self, i2c_address=DEFAULT_GPIOEXP_ADDR):
        """Initialize GPIO Expander with specified I2C address."""
        self._i2c_address = i2c_address
        self._i2c = None
        self._analog_write_resolution = 8
        self._analog_read_resolution = 10
    
    def begin(self, i2c=None):
        """Initialize the GPIO Expander with the specified I2C object.
        
        Args:
            i2c: machine.I2C object. If None, current I2C object is used.
        """
        if i2c:
            self._i2c = i2c
    
    def reset(self):
        """Reset the GPIO Expander chip."""
        self._write_cmd(IOcommand.RESET_SLAVE)
    
    def pin_mode(self, pin, mode):
        """Set pin mode.
        
        Args:
            pin: pin number
            mode: one of INPUT, OUTPUT, INPUT_PULLUP, INPUT_PULLDOWN
        """
        self.pin_mode_port(1 << pin, mode)
    
    def digital_write(self, pin, value):
        """Write digital value to a pin.
        
        Args:
            pin: pin number
            value: True for HIGH, False for LOW
        """
        send_data = 1 << pin
        if value:
            self._write_cmd_16bit_data(IOcommand.DIGITAL_WRITE_HIGH, send_data)
        else:
            self._write_cmd_16bit_data(IOcommand.DIGITAL_WRITE_LOW, send_data)
    
    def _map_resolution(self, value, from_res, to_res):
        """Map value from one resolution to another.
        
        Args:
            value: the value to map
            from_res: source resolution in bits
            to_res: target resolution in bits
        
        Returns:
            Mapped value
        """
        if from_res == to_res:
            return value
        if from_res > to_res:
            return value >> (from_res - to_res)
        else:
            return value << (to_res - from_res)
    
    def analog_write_resolution(self, res):
        """Set analog write resolution.
        
        Args:
            res: resolution in bits
        """
        self._analog_write_resolution = res
    
    def analog_read_resolution(self, res):
        """Set analog read resolution.
        
        Args:
            res: resolution in bits
        """
        self._analog_read_resolution = res
    
    def analog_write(self, pin, pulse_width):
        """Write analog value to pin.
        
        Args:
            pin: pin number
            pulse_width: PWM duty cycle value
        """
        val = self._map_resolution(pulse_width, self._analog_write_resolution, 16)
        self._write_cmd_pin_16val(IOcommand.ANALOG_WRITE, pin, val)
    
    def digital_read(self, pin):
        """Read digital value from pin.
        
        Args:
            pin: pin number
            
        Returns:
            1 for HIGH, 0 for LOW or -1 on error
        """
        port_value = self.digital_read_port()
        if port_value >= 0:
            return 1 if (port_value & (1 << pin)) else 0
        else:
            return 0
    
    def analog_read(self, pin):
        """Read analog value from pin.
        
        Args:
            pin: pin number
            
        Returns:
            Analog value or -1 on error
        """
        self._write_cmd_pin(IOcommand.ANALOG_READ, pin)
        result = self._read_16bit()
        if result >= 0:
            result = self._map_resolution(result, 12, self._analog_read_resolution)
        return result
    
    def change_addr(self, new_addr):
        """Change I2C address.
        
        Args:
            new_addr: new I2C address
        """
        self._write_cmd_8bit_data(IOcommand.CHANGE_I2C_ADDR, new_addr)
        self._i2c_address = new_addr
    
    def change_addr_with_uid(self, new_addr):
        """Change I2C address using device UID.
        
        Args:
            new_addr: new I2C address
        """
        uid = self.get_uid()
        
        sleep_ms(1)
        # Prepare data to send
        buffer = bytearray(5)
        buffer[0] = IOcommand.MASTER_READED_UID
        buffer[1] = (uid >> 24) & 0xff
        buffer[2] = (uid >> 16) & 0xff
        buffer[3] = (uid >> 8) & 0xff
        buffer[4] = uid & 0xff
        
        # Send the command and data
        self._i2c.writeto(self._i2c_address, buffer)
        
        sleep_ms(1)
        
        self._write_cmd_8bit_data(IOcommand.CHANGE_I2C_ADDR_IF_UID_OK, new_addr)
        self._i2c_address = new_addr
        
        sleep_ms(1)
    
    def save_addr(self):
        """Save current I2C address to device flash."""
        self._write_cmd(IOcommand.SAVE_I2C_ADDR)
    
    def pin_mode_port(self, value, mode):
        """Set pin mode for multiple pins at once.
        
        Args:
            value: bit mask of pins
            mode: one of INPUT, OUTPUT, INPUT_PULLUP, INPUT_PULLDOWN
        """
        if mode == INPUT:
            self._write_cmd_16bit_data(IOcommand.PORT_MODE_INPUT, value)
        elif mode == OUTPUT:
            self._write_cmd_16bit_data(IOcommand.PORT_MODE_OUTPUT, value)
        elif mode == INPUT_PULLUP:
            self._write_cmd_16bit_data(IOcommand.PORT_MODE_PULLUP, value)
        elif mode == INPUT_PULLDOWN:
            self._write_cmd_16bit_data(IOcommand.PORT_MODE_PULLDOWN, value)
    
    def digital_write_port(self, value):
        """Write digital values to multiple pins at once.
        
        Args:
            value: bit mask of pin values
        """
        self._write_cmd_16bit_data(IOcommand.DIGITAL_WRITE_HIGH, value)
        self._write_cmd_16bit_data(IOcommand.DIGITAL_WRITE_LOW, ~value)
    
    def digital_read_port(self):
        """Read digital values from all pins at once.
        
        Returns:
            Bit mask of pin values or -1 on error
        """
        self._write_cmd(IOcommand.DIGITAL_READ, False)
        return self._read_16bit()
    
    def pwm_freq(self, freq):
        """Set PWM frequency.
        
        Args:
            freq: frequency in Hz
        """
        self._write_cmd_16bit_data(IOcommand.PWM_FREQ, freq)
    
    def get_uid(self):
        """Get device unique ID.
        
        Returns:
            32-bit UID or 0xffffffff on error
        """
        self._write_cmd(IOcommand.UID)
        return self._read_32bit()
    
    def adc_speed(self, speed):
        """Set ADC conversion speed.
        
        Args:
            speed: speed value (0-7), smaller is faster but less accurate
        """
        self._write_cmd_8bit_data(IOcommand.ADC_SPEED, speed & 0x07)
    
    def adc_filter(self, enable):
        """Enable or disable ADC lowpass filter.
        
        Args:
            enable: True to enable, False to disable
        """
        command = IOcommand.ADC_LOWPASS_FILTER_ON if enable else IOcommand.ADC_LOWPASS_FILTER_OFF
        self._write_cmd(command)
    
    def set_encoder_pins(self, encoder, pin_a, pin_b):
        """Set encoder pins.
        
        Args:
            encoder: encoder number
            pin_a: encoder A pin
            pin_b: encoder B pin
        """
        pins = (pin_a << 4) | (pin_b & 0x0f)
        payload = (encoder << 8) | pins
        self._write_cmd_16bit_data(IOcommand.ENCODER_SET_PINS, payload)
    
    def read_encoder_diff(self, encoder):
        """Read encoder difference value.
        
        Args:
            encoder: encoder number
            
        Returns:
            Encoder difference value
        """
        self._write_cmd_pin(IOcommand.ENCODER_GET_DIFF_VALUE, encoder)
        return self._read_int8bit()
    
    # Low-level I2C communication methods
    def _write_cmd_pin(self, command, pin, send_stop=True):
        """Send command with pin number.
        
        Args:
            command: command code
            pin: pin number
            send_stop: whether to send stop condition
        """
        buffer = bytearray([command, pin])
        self._i2c.writeto(self._i2c_address, buffer)
    
    def _write_cmd_pin_16val(self, command, pin, value, send_stop=True):
        """Send command with pin number and 16-bit value.
        
        Args:
            command: command code
            pin: pin number
            value: 16-bit value
            send_stop: whether to send stop condition
        """
        buffer = bytearray([
            command,
            pin,
            (value >> 8) & 0xff,
            value & 0xff
        ])
        self._i2c.writeto(self._i2c_address, buffer)
    
    def _write_cmd_16bit_data(self, command, data):
        """Send command with 16-bit data.
        
        Args:
            command: command code
            data: 16-bit data
        """
        buffer = bytearray([
            command,
            (data >> 8) & 0xff,
            data & 0xff
        ])
        self._i2c.writeto(self._i2c_address, buffer)
    
    def _write_cmd_8bit_data(self, command, data):
        """Send command with 8-bit data.
        
        Args:
            command: command code
            data: 8-bit data
        """
        buffer = bytearray([command, data])
        self._i2c.writeto(self._i2c_address, buffer)
    
    def _write_cmd(self, command, send_stop=True):
        """Send command without data.
        
        Args:
            command: command code
            send_stop: whether to send stop condition
        """
        buffer = bytearray([command])
        self._i2c.writeto(self._i2c_address, buffer)
    
    def _read_int8bit(self):
        """Read signed 8-bit value from device.
        
        Returns:
            Signed 8-bit value or -1 on error
        """
        byte_count = 1
        try:
            data = self._i2c.readfrom(self._i2c_address, byte_count)
            if len(data) != byte_count:
                return -1
            # Convert to signed value
            value = data[0]
            if value > 127:
                value -= 256
            return value
        except Exception:
            return -1
    
    def _read_16bit(self):
        """Read 16-bit value from device.
        
        Returns:
            16-bit value or -1 on error
        """
        byte_count = 2
        try:
            data = self._i2c.readfrom(self._i2c_address, byte_count)
            if len(data) != byte_count:
                return -1
            result = (data[0] << 8) | data[1]
            return result
        except Exception:
            return -1
    
    def _read_32bit(self):
        """Read 32-bit value from device.
        
        Returns:
            32-bit value or 0xffffffff on error
        """
        byte_count = 4
        try:
            data = self._i2c.readfrom(self._i2c_address, byte_count)
            if len(data) != byte_count:
                return 0xffffffff
            result = 0
            for i in range(byte_count):
                result = (result << 8) | data[i]
            return result
        except Exception:
            return 0xffffffff
