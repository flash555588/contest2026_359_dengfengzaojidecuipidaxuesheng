#!/usr/bin/env bash
set -euo pipefail

readonly NUTTX_BASE="2f1387d56eb04ad2599baca58a3fa2380cdaaedb"
readonly APPS_BASE="88827afd368d4bbb4802b96ed44d9582f85b2f92"

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
contest_dir="$(cd "${script_dir}/.." && pwd)"
workspace_dir="$(cd "${contest_dir}/.." && pwd)"
state_dir="${workspace_dir}/.contest359-overlay-state"
checksum_file="${script_dir}/patches/SHA256SUMS"

die()
{
  echo "error: $*" >&2
  exit 1
}

patch_path()
{
  printf '%s/patches/%s' "${script_dir}" "${1%%:*}"
}

patch_strip()
{
  printf '%s' "${1##*:}"
}

affected_paths()
{
  local spec
  local patch
  local strip
  local path
  local i

  for spec in "$@"; do
    patch="$(patch_path "${spec}")"
    strip="$(patch_strip "${spec}")"

    while read -r path; do
      path="${path#b/}"
      for ((i = 1; i < strip; i++)); do
        path="${path#*/}"
      done

      printf '%s\n' "${path}"
    done < <(awk '/^diff --git / {print $4}' "${patch}")
  done | LC_ALL=C sort -u
}

tree_fingerprint()
{
  local tree="$1"
  shift
  local path

  while read -r path; do
    if [[ -L "${tree}/${path}" ]]; then
      printf 'link %s %s\n' "${path}" "$(readlink "${tree}/${path}")"
    elif [[ -f "${tree}/${path}" ]]; then
      printf 'file %s ' "${path}"
      sha256sum "${tree}/${path}" | awk '{print $1}'
    else
      printf 'missing %s\n' "${path}"
    fi
  done < <(affected_paths "$@") | sha256sum | awk '{print $1}'
}

verify_saved_state()
{
  local label="$1"
  local tree="$2"
  local expected_base="$3"
  local marker="$4"
  local patchset_sha="$5"
  shift 5
  local saved_base
  local saved_patchset
  local saved_result
  local actual_result

  [[ -f "${marker}" ]] || return 1

  saved_base="$(awk -F= '$1 == "base" {print $2}' "${marker}")"
  saved_patchset="$(awk -F= '$1 == "patchset" {print $2}' "${marker}")"
  saved_result="$(awk -F= '$1 == "result" {print $2}' "${marker}")"
  actual_result="$(tree_fingerprint "${tree}" "$@")"

  [[ "${saved_base}" == "${expected_base}" ]] ||
    die "${label}: saved base does not match ${expected_base}"
  [[ "${saved_patchset}" == "${patchset_sha}" ]] ||
    die "${label}: patch files changed after the saved application"
  [[ "${saved_result}" == "${actual_result}" ]] ||
    die "${label}: patched files changed after the saved application"

  echo "${label}: already applied and verified"
  return 0
}

apply_patch_set()
{
  local label="$1"
  local tree="$2"
  local expected_base="$3"
  local marker="$4"
  shift 4
  local patchset_sha
  local actual_base
  local spec
  local patch
  local strip
  local result_sha

  [[ -d "${tree}/.git" || -f "${tree}/.git" ]] ||
    die "${label}: missing Git tree ${tree}"

  actual_base="$(git -C "${tree}" rev-parse HEAD)"
  [[ "${actual_base}" == "${expected_base}" ]] ||
    die "${label}: expected base ${expected_base}, got ${actual_base}"

  patchset_sha="$({
    for spec in "$@"; do
      sha256sum "$(patch_path "${spec}")"
    done
  } | sha256sum | awk '{print $1}')"

  if verify_saved_state "${label}" "${tree}" "${expected_base}" \
      "${marker}" "${patchset_sha}" "$@"; then
    return 0
  fi

  [[ -z "$(git -C "${tree}" status --porcelain --untracked-files=all)" ]] ||
    die "${label}: source tree is dirty; refusing to apply patches"

  for spec in "$@"; do
    patch="$(patch_path "${spec}")"
    strip="$(patch_strip "${spec}")"
    git -C "${tree}" apply -p"${strip}" --whitespace=nowarn \
      --check "${patch}"
    git -C "${tree}" apply -p"${strip}" --whitespace=nowarn "${patch}"
    echo "${label}: applied ${spec%%:*}"
  done

  git -C "${tree}" diff --check
  result_sha="$(tree_fingerprint "${tree}" "$@")"
  mkdir -p "${state_dir}"
  printf 'base=%s\npatchset=%s\nresult=%s\n' \
    "${expected_base}" "${patchset_sha}" "${result_sha}" > "${marker}"
  echo "${label}: result ${result_sha}"
}

[[ -f "${checksum_file}" ]] || die "missing ${checksum_file}"
(cd "${script_dir}/patches" && sha256sum -c "${checksum_file}")

apply_patch_set "nuttx" "${workspace_dir}/nuttx" "${NUTTX_BASE}" \
  "${state_dir}/nuttx.state" \
  "0001-esp32p4-nuttx-overlay.patch:1" \
  "0003-esp32p4-sc2336-camera.patch:1" \
  "0004-esp32p4-camera-rgb565-preview.patch:1" \
  "0006-esp32p4-camera-maintainer-fixes.patch:2" \
  "0007-esp32p4-gt9xx-lifecycle.patch:2" \
  "0008-esp32p4-dsi-diagnostics.patch:2" \
  "0011-esp32p4-hil-test-config.patch:2" \
  "0013-esp32p4-hal-reproducibility.patch:1" \
  "0014-esp32p4-dsi-framebuffer-api-fix.patch:2" \
  "0015-third-party-provenance-comments.patch:2" \
  "0016-esp32p4-nxstyle-fixes.patch:2" \
  "0018-esp32p4-cmake-hal-parity.patch:2" \
  "0020-esp32p4-cmake-target-parity.patch:2" \
  "0022-esp32p4-csi-cmake-includes.patch:2" \
  "0024-esp32p4-cmake-link-completeness.patch:2" \
  "0026-esp32p4-cmake-image-tool-and-diag-config.patch:2"

apply_patch_set "apps" "${workspace_dir}/apps" "${APPS_BASE}" \
  "${state_dir}/apps.state" \
  "0002-esp32p4-apps-overlay.patch:1" \
  "0005-esp32p4-desktop-camera-preview.patch:2" \
  "0009-esp32p4-dsi-diag-app.patch:2" \
  "0010-esp32p4-desktop-string-bounds.patch:2" \
  "0012-apps-dependency-reproducibility.patch:2" \
  "0017-desktop-header-nxstyle-fix.patch:2" \
  "0019-quickjs-cmake-parity.patch:2" \
  "0021-desktop-cmake-support.patch:2" \
  "0023-desktop-cmake-font-path.patch:2" \
  "0025-dsi-diag-cmake-support.patch:2" \
  "0027-lvgl-extra-pages-reproducibility.patch:2"

echo "final overlays: all patch sets verified"
