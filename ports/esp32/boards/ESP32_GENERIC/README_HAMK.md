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

## GPIO 15 charging indicator

GPIO 15 is the primary LED used in the HAMK course. Native firmware owns it
only while fresh battery telemetry says that the charger is connected. The LED
then breathes with a three-second cycle. Before queued Python starts, firmware
turns the indicator off and releases GPIO 15; student code therefore retains
the same pin semantics as the course exercises. After Python exits, another
fresh battery sample is required before firmware can reclaim the pin.

Battery telemetry is considered fresh for 90 seconds. MQTT reconnect requests
a sample immediately, while the worker's regular battery polling keeps the
indicator current. The charging threshold is the same as the course docking
exercise: `charging >= 10`.

## Firmware identity and OTA validation

Every MQTT connection publishes a `hello` message containing:

- `firmware_revision`: the source Git revision compiled into MicroPython;
- `firmware_variant`: `HAMK_OTA` for this build;
- `bluetooth`: `false` for the shared robot firmware;
- `calibration_protocol`: the supported calibration message version.

A newly installed OTA image remains rollback-capable until Wi-Fi and MQTT have
been stable for 30 seconds and a valid battery sample has arrived. If those
checks do not pass within three minutes, the robot reboots so the ESP-IDF
bootloader can roll back. A second OTA is refused while validation is pending.
The existing `mark-valid` MQTT command remains an operator recovery override.

## Signed CI artifact

`.github/workflows/hamk_firmware.yml` produces OTA and serial-recovery images
whose filenames contain the full commit SHA. Their SHA-256 values are stored in
`manifest.json`; CI signs that manifest keylessly with Sigstore and uploads the
signature bundle with the package. Verify the bundle, then compare the selected
image's SHA-256 and size with the signed manifest before deploying it.
