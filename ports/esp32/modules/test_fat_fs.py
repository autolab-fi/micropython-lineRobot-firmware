# Access settings in your MicroPython code
import os
import json

def get_setting(key, default=None):
    return os.settings.get(key, default)

def update_setting(key, value):
    # Update in-memory settings
    os.settings[key] = value
    
    # Persist to FAT filesystem
    with open('/settings.json', 'w') as f:
        json.dump(os.settings, f)
    
    # Optional: Update SPIFFS copy (if needed for C code)
    # update_spiffs_copy()

# Example usage
broker = get_setting('broker_uri', 'mqtt://default')
print(f"Broker URI from settings: {broker}")