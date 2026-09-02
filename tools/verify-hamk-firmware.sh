#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 BUILD_DIRECTORY" >&2
    exit 2
fi

build_dir=$1
sdkconfig="$build_dir/sdkconfig"
firmware="$build_dir/micropython.bin"
linker_map="$build_dir/micropython.map"
ota_slot_size=$((0x180000))

for required_file in "$sdkconfig" "$firmware" "$linker_map"; do
    if [ ! -f "$required_file" ]; then
        echo "HAMK firmware verification failed: missing $required_file" >&2
        exit 1
    fi
done

if ! grep -q '^# CONFIG_BT_ENABLED is not set$' "$sdkconfig"; then
    echo "HAMK firmware verification failed: ESP-IDF Bluetooth is enabled" >&2
    exit 1
fi

if grep -Eiq 'mp_bluetooth|nimble|ble_gap|ble_gatt|libbt\.a' "$linker_map"; then
    echo "HAMK firmware verification failed: Bluetooth code is linked" >&2
    exit 1
fi

firmware_size=$(wc -c < "$firmware")
if [ "$firmware_size" -gt "$ota_slot_size" ]; then
    echo "HAMK firmware verification failed: $firmware_size bytes exceed the OTA slot" >&2
    exit 1
fi

free_bytes=$((ota_slot_size - firmware_size))
echo "HAMK firmware verified: Bluetooth absent, size=$firmware_size, OTA free=$free_bytes"
