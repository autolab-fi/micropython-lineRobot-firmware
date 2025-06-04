# Set location of base MicroPython directory.
if(NOT MICROPY_DIR)
    get_filename_component(MICROPY_DIR ${CMAKE_CURRENT_LIST_DIR}/../../.. ABSOLUTE)
endif()

# Set location of the ESP32 port directory.
if(NOT MICROPY_PORT_DIR)
    get_filename_component(MICROPY_PORT_DIR ${MICROPY_DIR}/ports/esp32 ABSOLUTE)
endif()


# idf_component_register(
#     SRCS "config_manager.c" "mqtt_client.c" "main.c"
#     INCLUDE_DIRS "."
# )

include(${MICROPY_PORT_DIR}/esp32_common.cmake)
