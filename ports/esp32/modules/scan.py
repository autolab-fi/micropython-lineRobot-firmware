from machine import I2C, Pin

def scan():
    i2c = I2C(0, scl=Pin(22), sda=Pin(21))
    print("I2C scan:", i2c.scan())