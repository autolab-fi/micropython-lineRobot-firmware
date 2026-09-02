set(SDKCONFIG_DEFAULTS
    ${SDKCONFIG_DEFAULTS}
    boards/ESP32_GENERIC/sdkconfig.ota
    boards/ESP32_GENERIC/sdkconfig.hamk_no_ble
)

list(APPEND MICROPY_DEF_BOARD
    MICROPY_HW_BOARD_NAME="HAMK line robot ESP32 with OTA"
    MICROPY_PY_BLUETOOTH=0
)
