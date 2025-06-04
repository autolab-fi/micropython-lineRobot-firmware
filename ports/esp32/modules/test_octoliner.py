import machine
from time import sleep
from octoliner import Octoliner
import math

def test():
    # Initialize I2C with appropriate pins for your board
    # Replace with your board's specific pins
    i2c = machine.I2C(scl=machine.Pin(22), sda=machine.Pin(21), freq=100000)
    
    # Create Octoliner instance with default address (42)
    octoliner = Octoliner()
    
    # Initialize Octoliner with the I2C interface
    octoliner.begin(i2c)
    
    # Set sensitivity (0-255)
    octoliner.set_sensitivity(230)
    
    # Alternatively, you can auto-calibrate sensitivity on black surface
    # Place the sensor over a black surface and uncomment:
    # if octoliner.optimize_sensitivity_on_black():
    #     print("Sensitivity optimized:", octoliner.get_sensitivity())
    # else:
    #     print("Failed to optimize sensitivity")
    
    print("Octoliner initialized")
    print("UID:", hex(octoliner.get_uid()))
    
    try:
        for i in range(100):
            # Read line position (-1.0 to 1.0, 0 is center)
            position = octoliner.track_line()
            
            # Read raw sensor values
            raw_values = octoliner.analog_read_all()
            
            # Convert analog values to binary pattern
            pattern = octoliner.map_analog_to_pattern(raw_values)
            
            # Print information
            print("Position: {:.2f}, Pattern: {:08b}".format(position, pattern))
            print("Raw values:", raw_values)
            
            # Print visual representation of the line position
            bar_width = 40
            center = bar_width // 2
            pos = int(position * center)
            bar = ["-"] * bar_width
            if not math.isnan(position):
                marker_pos = center + pos
                if 0 <= marker_pos < bar_width:
                    bar[marker_pos] = "O"
            print("".join(bar))
            
            sleep(0.1)
            
    except KeyboardInterrupt:
        print("Exiting...")
