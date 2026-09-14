#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
tree=${1:-/home/flash/openvela-contest359-camera-integration}
cc=/home/flash/vela-p4/riscv32-esp-elf/bin/riscv32-esp-elf-gcc
"$cc" -fsyntax-only -Wall -Wextra -Werror -Wno-unused-parameter -Wno-error=sign-compare \
  -march=rv32imac_zicsr_zifencei -mabi=ilp32 \
  -isystem "$tree/nuttx/include" -I "$tree/apps/graphics/lvgl" \
  -include nuttx/config.h -I "$tree/apps/system/desktop" \
  -I "$tree/apps/include" camera-app/esphome/esphome_lvgl.c \
  camera-app/esphome/esphome_client.c camera-app/esphome/esphome_model.c \
  camera-app/esphome/protobuf.c camera-app/desktop_main.c
make -C camera-app/esphome -f tests/Makefile p4-crypto CC="$cc" \
  P4FLAGS="-march=rv32imac_zicsr_zifencei -mabi=ilp32 -isystem $tree/nuttx/include -include nuttx/config.h"
