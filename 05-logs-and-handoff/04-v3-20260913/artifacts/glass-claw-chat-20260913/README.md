# ESP-Claw One-Shot Chat Candidate

Target: ESP32-P4 v3. Not flashed; no external model request was performed.
Retains the isolated desktop BLE, camera and Wi-Fi integration.

## Commands

`espclaw ask "question"` creates a core, submits one request, prints a successful
response, and destroys the core. No context persistence or tool execution is
enabled. `status`, `cleanup`, and the offline `selftest` remain available.

The caller supplies these process environment variables without writing them
to files: ESPCLAW_API_KEY, ESPCLAW_BASE_URL, ESPCLAW_MODEL, ESPCLAW_BACKEND.
The upstream compatible backend identifier is `openai_compatible`; it appends
`/chat/completions` to the supplied base URL. No service is configured by default.
Do not place credentials in shell scripts, saved history, logs, or chat messages.
Environment values remain owned by the caller and are not automatically erased.

Only HTTPS URLs without userinfo, query, fragment, spaces or backslashes are
accepted. This is input gating, not proof of endpoint trust or certificate validity.
The question is limited to 4096 bytes; the output budget is 512 model tokens.
Model timeout is 30 seconds, response wait is 35 seconds. Existing cooperative
transport cancellation means blocked system calls can prolong final cleanup.
Cleanup failure retains the core for explicit `espclaw cleanup`; no unsafe free
or restart is attempted. Error responses are not printed verbatim.

## Verification

ASan/UBSan agent regression passed: 20 two-round tool/restart cycles and in-flight
stop with a model double. Command fixture tests passed configuration rejection,
oversized input, successful/error response, cleanup timeout/retry, exclusion and
idempotent cleanup. These are not an end-to-end HTTP/TLS/model test.

RISC-V firmware link passed. Binary size: 3911116 bytes.
SHA-256: f38d729756c7cc8f8c11fbffbbd118ecf2a4e0f45113862dc9b76640b1d675ae.
Firmware and resolved config are in v3/; build output is in build.log.

Remaining: desktop chat UI, interactive cancellation, multi-turn session UX,
secure credential provisioning, target entropy/time/certificate verification,
and real service/board acceptance. Do not use this v3 image on v1.
