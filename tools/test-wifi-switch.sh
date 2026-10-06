#!/bin/sh
set -eu
repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test_bin=$(mktemp /tmp/hamk-wifi-test.XXXXXX)
trap 'rm -f "$test_bin"' EXIT HUP INT TERM
${CC:-cc} -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -I"$repo_dir/ports/esp32" \
    "$repo_dir/ports/esp32/wifi_switch.c" \
    "$repo_dir/ports/esp32/tests/test_wifi_switch.c" -o "$test_bin"
"$test_bin"
