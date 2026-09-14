#!/bin/bash
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive
apt-get install -y g++-multilib gcc-multilib libncurses-dev xxd gettext python3-setuptools python3-wheel
python3 -m pip install --index-url https://pypi.org/simple 'kconfiglib==14.1.0'
python3 -c "import kconfiglib; print('kconfiglib_ok')"
