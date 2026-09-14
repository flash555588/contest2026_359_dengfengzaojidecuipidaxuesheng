# V3 BLE Linked Candidate

Built in `/home/flash/glass-ble-v3-20260913`, independently copied from the
camera/Wi-Fi build tree. Original archived firmware was not replaced.

## Included

Raw HCI socket networking, exclusive C6 HCI ownership, serialized registration,
and the unmodified Apache NimBLE NuttX porting example are linked. `c6ble_main`,
`c6_ble_register`, `esp_hosted_hci_claim`, and `nimble_main` were found in the ELF.
The example provides peripheral advertising and GATT alert services; it is not
a desktop central-mode scanner or general GATT client. Requested central roles
alone do not establish that those operations are implemented in the application.

NimBLE source commit: `b6831813cfe6da9b25c8be7136b40ce060bf9710`, matching the
streetartist apps repository configuration inspected during this work.
Archive SHA-256: `1632fb4a5b351af5a3e9761b36d89bc35161524014b9c21097f0b2d3525cf0f5`.
Source licenses and notices are retained in the isolated source tree.

Configuration enables WIRELESS, ALLOW_BSD_COMPONENTS, WIRELESS_BLUETOOTH,
NET_BLUETOOTH, SYSTEM_C6BLE_V3_EXPERIMENTAL, NIMBLE and SIG_EVTHREAD.
WIRELESS_BLUETOOTH_HOST is disabled to avoid competing host stacks.
The porting example stack is 16384 bytes; the NPL callout stack is 8192 bytes.
Claw compilation retains the existing three ESPCLAW_TLS_CFLAGS include paths.

## Verification

Full RISC-V build and image generation succeeded. `build.log` contains the final
build output. Earlier attempts identified missing wireless dependencies,
an incorrect default NimBLE revision, SIGEV_THREAD, and Claw TLS include flags.
The resolved configuration is saved alongside the ELF and binary.

BIN size: 3908844 bytes. Flash offset remains 0x2000, leaving 277268 bytes before
the /data partition at 0x400000. BIN SHA-256:
`ad2755e88f694661cd9898c3cd71e850b0145627da089e0a9f242f39ab6f900a`.

## Unverified

No flashing or radio operations were performed. C6 capability negotiation,
HCI reset/events, advertising visibility, pairing, Wi-Fi coexistence, stack
margin and shutdown behavior require hardware verification. Desktop Bluetooth
remains a placeholder. General scanning, connection management and GATT client
operations are not delivered by this candidate. Do not flash to v1.
