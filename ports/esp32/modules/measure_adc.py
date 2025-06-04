
from machine import I2C, Pin
from ads1x15 import ADS1115

def measure():
   i2c=I2C(0, scl=Pin(22), sda=Pin(21))
   adc = ADS1115(i2c, address=72, gain=1)
   value = adc.read(0, 0)
   value1 = adc.read(0,1)
   print(f"SYS{{0: {value}, 1: {value1}}}")
