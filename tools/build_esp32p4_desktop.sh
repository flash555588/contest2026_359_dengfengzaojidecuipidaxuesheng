#!/usr/bin/env bash

set -euo pipefail

usage() {
  echo "Usage: ESP_RISCV_TOOLCHAIN=/path/to/bin $0 <v1.0|v3.2> [output-dir]" >&2
  exit 2
}

[[ $# -ge 1 && $# -le 2 ]] || usage

variant="$1"
workspace="${OPENVELA_ROOT:-$(pwd)}"
toolchain="${ESP_RISCV_TOOLCHAIN:-}"
jobs="${BUILD_JOBS:-$(nproc)}"

case "$variant" in
  v1.0)
    config="esp32p4-function-ev-board:desktop-v1"
    ;;
  v3.2)
    config="esp32p4-function-ev-board:desktop"
    ;;
  *)
    usage
    ;;
esac

[[ -n "$toolchain" ]] || {
  echo "ESP_RISCV_TOOLCHAIN must point to the riscv32-esp-elf bin directory" >&2
  exit 1
}

[[ -x "$toolchain/riscv32-esp-elf-gcc" ]] || {
  echo "Compiler not found: $toolchain/riscv32-esp-elf-gcc" >&2
  exit 1
}

[[ -d "$workspace/nuttx" && -d "$workspace/apps" ]] || {
  echo "OPENVELA_ROOT must contain nuttx/ and apps/" >&2
  exit 1
}

compiler_version="$($toolchain/riscv32-esp-elf-gcc --version | head -n 1)"
[[ "$compiler_version" == *"15.2.0"* ]] || {
  echo "Expected GCC 15.2.0, got: $compiler_version" >&2
  exit 1
}

kconfig_tweak="$(command -v kconfig-tweak || true)"
[[ -x "$kconfig_tweak" ]] || {
  echo "kconfig-tweak is required by the OpenVela configure flow" >&2
  exit 1
}

output="${2:-$workspace/out/esp32p4-desktop-$variant}"
mkdir -p "$output"

export PATH="$toolchain:$(dirname "$kconfig_tweak"):/usr/local/bin:/usr/bin:/bin"
export CROSSDEV=riscv32-esp-elf-

cd "$workspace/nuttx"
./tools/configure.sh -E -S -l -a ../apps "$config"
make -j"$jobs"

cp nuttx.bin "$output/nuttx.bin"
cp nuttx "$output/nuttx.elf"
cp nuttx.hex "$output/nuttx.hex"
cp .config "$output/resolved.config"
cp "boards/risc-v/esp32p4/esp32p4-function-ev-board/configs/${config##*:}/defconfig" \
  "$output/defconfig"

git_head() {
  git -C "$1" rev-parse HEAD 2>/dev/null || echo unavailable
}

quickjs_source="$(awk '/^QUICKJS_COMMIT_ID[[:space:]]*=/{print $3; exit}' \
  "$workspace/apps/interpreters/quickjs/Makefile")"
hal_root="${NXTMPDIR:-$workspace/nxtmpdir}/esp-hal-3rdparty"

{
  echo "variant=$variant"
  echo "config=$config"
  echo "compiler=$compiler_version"
  echo "compiler_sha256=$(sha256sum "$toolchain/riscv32-esp-elf-gcc" | cut -d' ' -f1)"
  echo "nuttx=$(git rev-parse HEAD)"
  echo "apps=$(git -C ../apps rev-parse HEAD)"
  echo "lvgl=$(git_head "$workspace/apps/graphics/lvgl/lvgl")"
  echo "quickjs_source=${quickjs_source:-unavailable}"
  echo "vendor_espressif=$(git_head "$workspace/vendor/espressif")"
  echo "esp_hal=$(git_head "$hal_root")"
  echo "mbedtls=$(git_head "$hal_root/components/mbedtls/mbedtls")"
} > "$output/BUILD-METADATA.txt"

(
  cd "$output"
  sha256sum nuttx.bin nuttx.elf nuttx.hex defconfig resolved.config \
    BUILD-METADATA.txt > SHA256SUMS
)

echo "Build completed: $output"
