
from machine import I2C, Pin
from ads1x15 import ADS1115

def measure():
   i2c=I2C(0, scl=Pin(22), sda=Pin(21))
   adc = ADS1115(i2c, address=72, gain=1)
   value = adc.read(0, 0)*4.096/(32768>>0)*10
   value1 = adc.read(0,1)*4.096/(32768>>0)*10
   return value, value1
   
def measure_print():
   v1, v2 = measure()
   ch = 'false'
   if v2>5:
      ch='true'
   print(f"SYS{{'voltage': {v1}, 'charging': {v2}}}")
