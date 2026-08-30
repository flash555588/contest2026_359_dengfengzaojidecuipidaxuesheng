#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
contest_dir="$(cd "${script_dir}/.." && pwd)"
workspace_dir="$(cd "${contest_dir}/.." && pwd)"

apply_one()
{
  tree_path="$1"
  patch_path="$2"
  label="$3"

  if git -C "${tree_path}" apply --reverse --check "${patch_path}"; then
    echo "${label}: already applied"
  elif git -C "${tree_path}" apply --check "${patch_path}"; then
    git -C "${tree_path}" apply "${patch_path}"
    echo "${label}: applied"
  else
    echo "${label}: patch does not match this source baseline" >&2
    return 1
  fi
}

apply_one "${workspace_dir}/nuttx" \
  "${script_dir}/patches/0001-esp32p4-nuttx-overlay.patch" "nuttx"
apply_one "${workspace_dir}/apps" \
  "${script_dir}/patches/0002-esp32p4-apps-overlay.patch" "apps"
