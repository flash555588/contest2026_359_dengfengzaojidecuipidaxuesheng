#!/bin/bash
set -euo pipefail
# Initialize openvela contest workspace on WSL ext4 (not /mnt/c).
ROOT=/home/flash/openvela
mkdir -p "$ROOT"
cd "$ROOT"
git lfs install --skip-repo
if [ -d .repo ]; then
  echo "INFO: .repo already exists, skip repo init"
else
  repo init \
    -u https://github.com/open-vela/contest2026_359_dengfengzaojidecuipidaxuesheng \
    -b dev-ai-contest-2026 \
    -m contest2026_359_dengfengzaojidecuipidaxuesheng.xml \
    --repo-url=https://mirrors.tuna.tsinghua.edu.cn/git/git-repo/ \
    --git-lfs
fi
echo "INIT_OK $(pwd)"
