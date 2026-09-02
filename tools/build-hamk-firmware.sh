#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir=${HAMK_BUILD_DIR:-build-ESP32_GENERIC-HAMK_OTA}

make -C "$repo_dir/mpy-cross" "${@}"
make -C "$repo_dir/ports/esp32" \
    BOARD=ESP32_GENERIC \
    BOARD_VARIANT=HAMK_OTA \
    BUILD="$build_dir" \
    "${@}"

case "$build_dir" in
    /*) absolute_build_dir=$build_dir ;;
    *) absolute_build_dir="$repo_dir/ports/esp32/$build_dir" ;;
esac

"$repo_dir/tools/verify-hamk-firmware.sh" "$absolute_build_dir"
