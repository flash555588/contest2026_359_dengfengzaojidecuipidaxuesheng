# Optional C6 Wireless Overlay

RPC mailbox integration: tools/adapt_c6_rpc_mailbox.py now applies a real
mutex-protected response slot after the size and transaction-serialization
patches. Each transaction uses a new nonzero UID; IDs are not reused after
exhaustion. Duplicate, late and mismatched responses remain owned by RX and
are freed there. Closing the response window transfers accepted ownership
to the caller atomically; send/poll failure drains and frees accepted replies.
Two unused legacy busy assignments were removed. The actual RPC source passed
RISC-V compilation in a temporary copy; mailbox race and size regressions passed.
The adaptation was then applied to the isolated openvela tree; a second run
made no changes. It has not been included in a new flashed firmware.

Remaining concurrency work includes shared Hosted receive-buffer dispatch and
callback registration with multiple pollers. This mailbox alone does not make
simultaneous CLI/network-daemon/desktop operation safe. Structured scan-result
delivery and desktop worker integration remain pending.

RPC caller serialization: hosted-rpc-serialize.patch wraps complete RPC
transactions in a pthread mutex. The actual wrapper passed 200 calls across
four host threads, including failed transactions; target RISC-V compilation
and the serialization-size regression also passed. Apply after hosted-rpc-size.
This does not synchronize the asynchronous RX callback with timeout cleanup,
fix reused UIDs, or make the shared Hosted receive buffer safe with concurrent
pollers. RX/event callbacks must not recursively issue synchronous RPCs.
Do not enable concurrent desktop/c6net/CLI operation until these remaining
ownership issues are resolved. No new firmware was flashed in this step.

RPC hardening: hosted-rpc-size.patch checks rpc__get_packed_size before
serializing into the 256-byte request buffer, and checks the returned packed
length before TLV composition. The previous order wrote before checking.
test_rpc_size.py exercises the actual serialization prefix with mocked
protobuf operations: 257-byte rejection, 256-byte boundary and empty input
passed ASan/UBSan. The patched RPC source also compiled for RISC-V against
the current isolated openvela tree. No new firmware was flashed for this fix.

The WSL integration tree still exists; an earlier Windows-only path check
incorrectly reported it missing. Desktop C6 status/scan worker integration is
not complete. The c6_status files in the external reference directory remain
unintegrated placeholders and must not be treated as synchronized live status.

Latest build: the explicit openvela-cmd53-read-errors.patch checks receive
setup and command submission results, cancels waiting and unlocks on failure.
The actual-function regression covers ENOSYS, send failure and success.
The independent v1 build completed with DMA enabled; its exported candidate
is artifacts/c6-wifi-v1-dma-cmd53-20260911 in the parent workspace, 3,707,424
bytes. Image checksum, configuration, symbols and hashes passed. It has not
been flashed or radio-tested. Earlier board timeout results concern older
non-DMA firmware and must not be attributed to this new candidate.

Latest correction: configure_c6.py now enables and verifies both
ESP32P4_SDMMC_DMA and SDIO_DMA. The former non-DMA setting was incompatible
with openvela's CMD53 helper, which calls DMA setup and ignores ENOSYS when
DMA is disabled. The v1 180 MHz DMA candidate fully compiled and was exported
to artifacts/c6-wifi-v1-dma-20260911 in the parent workspace. Its final ELF
contains both DMA setup functions; image checksum and file hashes passed.
It has not been flashed or radio-tested. Earlier non-DMA instructions below
are historical, not the current required configuration.

Status: source delivery only, not a working Wi-Fi/Bluetooth release.

2026-09-11: full v1 build succeeded in the isolated integration tree after
also applying openvela-locks.patch (nuttx) and openvela-net-lock.patch (apps).
These add the real spinlock/mutex declarations, without changing lock semantics.
The final ELF contains c6probe_main, esp_hosted_initialize and c6net_initialize;
the 180 MHz BIN is 3,706,116 bytes. Export is in the parent workspace's
artifacts/c6-wifi-v1-20260911. Image checksum and file hashes passed.
No firmware was flashed and no C6 command was executed. The compatibility
patches remain explicit separate steps, not automatically managed by the base
overlay script. TLS archives must be revalidated for the changed configuration
before online TLS acceptance. Bluetooth HCI stack adaptation remains pending.

Configuration progress: configure_c6.py resolves SDMMC, c6probe and Ethernet
dependencies in the isolated v1 workspace. It verifies Function-EV pins and
sets MMCSD_MULTIBLOCK_LIMIT=128, as required by the reference driver; DMA is
disabled for initial bring-up. The command registry now contains c6probe.
The openvela tree uses debug.h, not nuttx/debug.h: apply the separate
patches/c6/openvela-debug.patch after the reference overlays (check with
git apply --check first). This compatibility patch is not yet automatically
managed by apply_c6_overlays.py. It was applied to the isolated v1 workspace.
After these corrections, top-level pass2dep succeeded. Full compilation,
linking, radio handshake and Wi-Fi/BLE operation remain unverified.

Example configuration (WSL):
```
python3 tools/configure_c6.py /path/to/isolated-workspace \
  --toolchain /path/to/riscv32-esp-elf/bin --configure
```

Pinned streetartist feature/esp-hosted-c6 patches are stored in patches/c6
with commit identities and SHA256 in manifest.json. The kernel patch includes
only SDMMC source/header and Kconfig/Make integration. The applications patch
contains system/c6probe, Hosted RPC and c6net. Camera, Flash and desktop changes
from the reference branch are intentionally excluded.

Run `python3 tools/apply_c6_overlays.py /path/to/workspace` for check-only mode.
The workspace must contain nuttx and apps. Both patches are verified and
preflighted before writes. Explicit --apply applies them; it does not configure,
build, flash or start the radio. Do not run concurrent workspace edits.

The Make.defs patch now anchors on the SMP block rather than the preceding
CHIP_CSRCS line, preserving openvela's esp_atomic64.c addition. Both patches
passed preflight and were applied to the isolated glass-claw-v1-20260910
workspace. Repeated checks passed reverse application, confirming the applied
state. Three offline tests cover hashes/scope, read-only checks and preservation
of the atomic source through a real patch application. Reference protobuf
sources retain upstream trailing-whitespace warnings.

Configuration resolution, NuttX API compatibility and compilation still need
validation before enabling the radio. This optional overlay is not silently
added to apply_final_overlays.sh. No c6probe command or C6 reset was executed.

Reference wiring: SDIO slot 1, CMD19, CLK18, D0..D3=14..17, C6 reset GPIO54.
The reference follows ESP-Hosted 2.12.12. c6probe initialization resets C6;
it is not a read-only query. Installed C6 firmware and wiring remain to be
verified. Wi-Fi scan/association code exists; Bluetooth has HCI framing but no
registered NuttX controller adapter, so BLE is not yet available.

No new BIN, radio connection, or board verification is claimed. Run offline
delivery checks with `python3 tools/tests/test_c6_overlays.py`.

The board test returned `R4: -22` before this correction. The SDMMC
`recvshort()` callback was assigned to R4 but its debug validation accepted
only R3/R7; the CRC short-response callback also omitted R5. The separate
`openvela-sdio-responses.patch` adds valid R4/R5 response types while keeping
timeout and CRC checks. The extracted-function regression passes legal R4/R5,
wrong-type rejection, timeout and CRC failure cases. Apply it before the next
build and repeat the probe; no radio result is inferred from this host test.
