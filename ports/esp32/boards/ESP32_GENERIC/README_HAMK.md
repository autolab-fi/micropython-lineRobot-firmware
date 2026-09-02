# HAMK line robot firmware

Build the laboratory firmware with the dedicated `HAMK_OTA` variant:

```sh
tools/build-hamk-firmware.sh -j2
```

The robot communicates through Wi-Fi and MQTT. Bluetooth is deliberately
disabled at both layers:

- `sdkconfig.hamk_no_ble` removes the ESP-IDF Bluetooth controller and host;
- `mpconfigvariant_HAMK_OTA.cmake` removes MicroPython's `bluetooth` module.

The build wrapper runs `tools/verify-hamk-firmware.sh` after linking. The check
fails if Bluetooth symbols are present or the application exceeds the 1.5 MiB
OTA slot. Do not build a HAMK release with the generic `OTA` variant: it enables
Bluetooth and does not fit the robot's OTA partition layout.
