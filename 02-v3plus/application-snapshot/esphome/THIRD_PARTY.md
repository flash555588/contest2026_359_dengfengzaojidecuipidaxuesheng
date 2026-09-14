# Sources and Adaptation Status

The on-device app is C/LVGL. It adapts the connection, enumeration,
subscription and command workflow of the Python client; it does not run the
ESPHome Python configuration compiler on NuttX.

- ESPHome reference: `e3dd2f44a45bc7200566393db51fa17eb0a5edf1`.
  Copied protocol and Python reference files are in `reference/`.
  The original mixed-license notice is preserved in `reference/ESPHOME-LICENSE`.
  That notice explicitly licenses the Python codebase and other non-runtime
  parts under MIT; GPLv3 applies to the listed C/C++ runtime file extensions.
- aioesphomeapi reference: `46475a3b8767bd28c4487fc263444798a34b5ede`.
  Python reference files and its MIT license are in `reference/`.
- esphome/noise-c: `b3da54dc1020150237054004c5fdbffc63a23538`,
  version 0.1.21. The source archive is retained beside the extracted tree.
  Original notices remain in `third_party/noise-c/LICENSE`, `COPYING` and
  individual crypto source files, including the X25519 license.

Only the selected C crypto sources in `crypto.mk` enter the firmware.
Python references and the ESPHome C++ server runtime are not linked.
The Noise Curve25519 adapter has local changes for fallible OS entropy and
rejecting all-zero shared secrets. `noise_port.c` is the platform adapter.
Do not remove or replace upstream license notices when distributing sources.

Hardware qualification remains required: `/dev/random` being present does
not establish that the ESP32-P4 physical entropy source is initialized.
Verify the board RNG initialization and camera coexistence before deploying
encrypted control. No fallback PRNG or plaintext downgrade is provided.

Offline host checks (from this directory, under WSL):

```sh
make -f tests/Makefile OUT=/home/flash/esphome-tests check
bash tests/check-p4.sh
```

Host tests do not substitute for actual ESPHome interoperability, touchscreen
tests, firmware linkage, or device qualification. No credentials are stored.
