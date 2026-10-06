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

## Remote Wi-Fi provisioning (protocol 1)

The `hello` message advertises `wifi_protocol: 1`. Older firmware does not
support the commands below. `set-coeff` only accepts whitelisted numeric
calibration coefficients; the legacy `settings_writer/mqtt_writer.py` must not
be used to change Wi-Fi settings.

Publish JSON with QoS 1 and **retain=false** to `<topic_system>/input`; subscribe
to `<topic_system>/output` first. On HAMK device 13 these are
`lfmp1/system/input` and `lfmp1/system/output`. Retained commands are rejected.
Use an authenticated, authorized operator connection and TLS (`mqtts://`) to
protect credentials in transit. Plain `mqtt://` does not encrypt passwords.

1. Query the current network (no password is returned):

   ```json
   {"command":"wifi-status"}
   ```

2. Start a trial with a fresh request ID (1–48 bytes):

   ```json
   {"command":"wifi-configure","request_id":"network-change-001","ssid":"LabRobotics","password":"<Wi-Fi password>"}
   ```

   The `pending` response precedes the switch by one second. The device then
   applies credentials in RAM and reconnects to the same MQTT broker. It emits
   `ready` after Wi-Fi and MQTT have been connected for five seconds. Status
   includes the request ID, target SSID, current SSID, IP, RSSI and remaining
   trial time. If a response is missed, query `wifi-status` again.

3. Only after receiving `ready` for the matching ID and verifying the SSID,
   confirm over the new connection:

   ```json
   {"command":"wifi-confirm","request_id":"network-change-001"}
   ```

   Wait for `committed` (or query status again). This saves SSID and password
   together as one NVS blob. Subsequent boots use the confirmed pair.

Without confirmation within **120 seconds of acceptance**, the previous driver
configuration is restored, including when Wi-Fi works but the broker cannot
be reached. An explicit `wifi-cancel` with the same request ID also restores it.
An unconfirmed trial is RAM-only: rebooting uses the previously saved network.
Restoring connectivity still requires that the previous network is available.
The last request ID is remembered until reboot so QoS 1 duplicates do not
restart a completed/failed trial. Use a fresh ID for a deliberate retry.

Only WPA2-personal-compatible networks are supported: SSID 1–32 bytes and
printable ASCII passphrase 8–63 bytes, or a 64-digit hexadecimal PSK. Open,
enterprise/802.1X and WPA3-only networks are not supported by this command.
The ESP32 robot requires 2.4 GHz coverage even when the Raspberry Pi can use
the same SSID on 5 GHz.

Switching requires an idle robot and no active/pending OTA validation. While
the trial is active, movement/programming/OTA MQTT commands are refused;
already queued Python waits until maintenance ends. The existing OTA handler
also reserves the same maintenance lock. Battery queries and ping remain
available. Do not submit laboratory jobs during network maintenance.

Existing installations initially read Wi-Fi from SPIFFS `settings.json`.
After the first confirmed update, NVS namespace `robot_network`, key `wifi_v1`,
is authoritative. UART `set string wifi_ssid=...;` / `wifi_pass=...;` and normal
settings reads use this pair too. The SPIFFS file and an older firmware image
may still contain the old network; downgrading to pre-protocol firmware does
not read the NVS override. Raw MQTT payloads, automatic settings mirroring and
Wi-Fi connection logs no longer print the supplied password.

Run `tools/test-wifi-switch.sh` for host-side state-machine tests (requires a C
compiler and address/undefined-behavior sanitizers), then build with the HAMK
wrapper. Before deployment, exercise successful confirmation, bad password,
broker-unreachable network, no confirmation and reboot-during-trial on a spare
robot with serial recovery available. Host tests do not validate radio/NVS
power-loss behavior on real hardware.
