import machine
from tcs3472 import tcs3472
import time

def test():
    bus = machine.I2C(sda=machine.Pin(21), scl=machine.Pin(22)) # adjust pin numbers as per hardware
    tcs = tcs3472(bus)

    for i in range(100):
       print("Light:", tcs.light())
       print("RGB:", tcs.rgb())
       time.sleep(1)
