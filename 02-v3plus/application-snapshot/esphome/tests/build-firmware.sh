#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
source=/home/flash/openvela-contest359-camera-integration
tree=${1:-$(mktemp -d /home/flash/esphome-firmware-XXXXXX)}
case "$(realpath "$tree")" in
  /home/flash/esphome-firmware-*) ;;
  *) printf 'Not an isolated ESPHome build tree\n' >&2; exit 1 ;;
esac
printf 'Isolated build: %s\n' "$tree"
if [ ! -d "$tree/nuttx" ]; then
  cp -a "$source/nuttx" "$source/apps" "$tree/"
fi
python3 "$root/tests/relocate_baseline_links.py" "$source" "$tree"
desktop="$tree/apps/system/desktop"
cp "$root/desktop_main.c" "$root/Makefile" "$desktop/"
cp "$root/pomodoro_lvgl.c" "$root/pomodoro_lvgl.h" \
   "$root/pomodoro_engine.c" "$root/pomodoro_engine.h" \
   "$root/qpk_pomodoro.c" "$root/qpk_pomodoro.h" \
   "$root/pomodoro_resource.c" "$desktop/"
cp -a "$root/esphome" "$desktop/"
export PATH=/home/flash/.local/bin:/home/flash/vela-p4/riscv32-esp-elf/bin:$PATH
export CROSSDEV=riscv32-esp-elf-
export GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=protocol.allow GIT_CONFIG_VALUE_0=never
make -C "$tree/nuttx" clean > "$tree/clean.log" 2>&1
make -C "$tree/nuttx" -j4 DOWNLOAD=false CURL=false WGET=false > "$tree/build.log" 2>&1 || {
  tail -n 70 "$tree/build.log"
  exit 1
}
"${CROSSDEV}nm" "$tree/nuttx/nuttx" | grep ' T esphome_lvgl_create$'
printf 'Firmware: %s/nuttx/nuttx.bin\n' "$tree"
