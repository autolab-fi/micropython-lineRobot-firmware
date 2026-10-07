# MicroPython 1.29 robot firmware migration

This branch merges upstream MicroPython v1.29.0 into the robot firmware based on
8f664709b. ESP-IDF stays at 5.4.1 (supported by upstream v1.29.0), and the target
remains ESP32_GENERIC / HAMK_OTA with Bluetooth disabled.

## Robot-specific integration

- The custom MQTT, Wi-Fi provisioning, coefficients, OTA, charging indicator,
  correlated Stop and boot-epoch fencing services remain in place.
- The custom `mp_task` stays in `micropython_task.c`. Upstream peripheral cleanup
  is applied there, including `esp32_pcnt_deinit_all()` before releasing the VM.
  Upstream's duplicate `mp_task` is excluded from `main.c`.
- `Robot` uses the standard `machine.Encoder` on PCNT units 0 and 1, with four
  phases and a 1250 ns filter. Right-wheel polarity remains forward-positive.
  Public encoder positions, radians/degrees, resets and motion methods remain
  available. The custom experimental `robot_encoders` C module is not included.
- PWM is constructed with `duty=0`. Previously it was constructed at the default
  50% duty before later being cleared, which could cause a startup motor pulse.
- The HAMK 4 MiB OTA partition layout retains its fixed VFS at 0x310000. OTA
  installs only the application, not the bootloader or partition table.
- Stored calibration and network settings are not rewritten by this migration.

## Verification

Host tests:

```
python3 -m unittest discover -s ports/esp32/tests -p test_robot_encoder_api.py
tools/test-wifi-switch.sh
tools/build-hamk-firmware.sh -j4
```

The host encoder tests use a fake `machine.Encoder` and do not establish physical
accuracy. Hardware acceptance covers signed counts, both overflow directions,
reset/assignment semantics, repeated initialization and soft resets, Stop and a
fresh program, camera-observed turns/straight travel, and docking.

When generating synthetic pulses in Python, yield to FreeRTOS regularly with a
sleep of at least one tick. A long busy loop can starve the idle task watched by
the firmware's existing watchdog. Synthetic register-routing tests are only for
an exclusively reserved, stationary robot; they must restore GPIO routing and
must not be mixed with physical encoder motion.

The test firmware is identified as `mp129-encoder-test`; the exact binary SHA256
and physical evidence are kept in the deployment report. A successful build or
OTA health validation alone does not establish movement calibration accuracy.
