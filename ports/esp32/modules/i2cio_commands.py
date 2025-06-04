"""
This file is a part of Octoliner library for MicroPython.

Original product page: https://amperka.ru/product/zelo-folow-line-sensor
© Amperka LLC (https://amperka.com, dev@amperka.com)

Original Authors: Vasily Basalaev <vasily@amperka.ru>, Yury Botov <by@amperka.com>
MicroPython port by: [Your Name]
License: GPLv3, all text here must be included in any redistribution.
"""

# Enum of I2C commands for GPIO Expander communication
class IOcommand:
    # Basic functions
    UID = 0x00
    RESET_SLAVE = 0x01
    CHANGE_I2C_ADDR = 0x02
    SAVE_I2C_ADDR = 0x03
    PORT_MODE_INPUT = 0x04
    PORT_MODE_PULLUP = 0x05
    PORT_MODE_PULLDOWN = 0x06
    PORT_MODE_OUTPUT = 0x07
    DIGITAL_READ = 0x08
    DIGITAL_WRITE_HIGH = 0x09
    DIGITAL_WRITE_LOW = 0x0A
    ANALOG_WRITE = 0x0B
    ANALOG_READ = 0x0C
    PWM_FREQ = 0x0D
    ADC_SPEED = 0x0E
    MASTER_READED_UID = 0x0F
    CHANGE_I2C_ADDR_IF_UID_OK = 0x10
    SAY_SLOT = 0x11
    
    # Advanced ADC functions
    ADC_LOWPASS_FILTER_ON = 0x20
    ADC_LOWPASS_FILTER_OFF = 0x21
    ADC_AS_DIGITAL_PORT_SET_TRESHOLD = 0x22
    ADC_AS_DIGITAL_PORT_READ = 0x23
    
    # Encoder functions
    ENCODER_SET_PINS = 0x30
    ENCODER_GET_DIFF_VALUE = 0x31
    
    # Advanced PWM functions
    PWM_ANALOG_WRITE_U8 = 0x40
    
    # Etc - start at 0xE0
    ETC_VERSION = 0xE0
    ETC_ACT_LED_ENABLE = 0xE1
    ETC_ACT_LED_DISABLE = 0xE2
    ETC_ACT_LED_BLINK_WITH_COUNTER = 0xE3
    ETC_NUM_DIGITAL_PINS = 0xE4
    ETC_NUM_ANALOG_INPUTS = 0xE5

# Constants
INPUT = 0
OUTPUT = 1
INPUT_PULLUP = 2
INPUT_PULLDOWN = 3

DEFAULT_GPIOEXP_ADDR = 42
I2CIO_PIN_COUNT = 10

# Magic number used for device identification
IS_HE_SAY_SLOT = (ord('s') << 24) | (ord('l') << 16) | (ord('o') << 8) | ord('t')
