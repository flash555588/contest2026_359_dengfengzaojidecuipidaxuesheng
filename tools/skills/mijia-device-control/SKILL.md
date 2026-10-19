---
name: mijia-device-control
description: Develop Mijia device detail controls and AI voice tools in an openvela/ESPClaw project using its Home Assistant bridge. Use for device subfunctions, capability discovery, multi-entity grouping, parameter validation, and control readback. This skill does not log into the official Mijia app or grant access to a household.
---

# 米家设备功能控制

Apply this workflow to the project's existing Home Assistant integration. Locate the repository and applicable instructions before editing; read [references/project.md](references/project.md) when source paths, build commands, or device behavior are needed.

## Discover the actual function

- Distinguish a physical device from an HA entity. Use registry `device_id` / `device_entities` associations to group functions. Equal names, room names, or model strings do not establish membership.
- Inspect actual state attributes, available services, supported-feature flags, numeric bounds/steps and option lists. A MIoT product type is a presentation hint, not permission to issue a service.
- Preserve the exact target, action, number, unit and negation from the current request. Resolve ambiguity rather than inventing a default, reusing an earlier target, guessing an enum, or changing every entity on a device.
- An auxiliary child-lock switch, indicator switch, select or timer can be a separate entity. Send to that entity, not to the device's primary light/fan entity.

## Keep the control paths consistent

For a new function, check the shared typed-service rules, detail-page normalization and controls, AI discovery schema, action dispatch, readback mapping and result-card wording. Update only affected paths.

- Require one exact entity and only the parameters registered for its service. Reject duplicate fields, extra targets, wrong types, unknown enum values and out-of-range values. Do not silently clamp AI requests.
- Check the entity capability again before a delayed control is submitted. Preserve queue order while coalescing the same unsent property; a new value must not silently replace a different property.
- Return actual option strings and bounds to AI. Preserve advertised values when translating labels for the UI. Temperature low/high bounds must be sent as a valid pair.
- Keep AI permission checks and existing lock/security exclusions. Never enable broader control permissions merely to make a test pass. Do not print tokens or put household credentials in this skill or the repository.

## Confirm the result honestly

A successful HTTP response proves acceptance. Confirm a stateful action by reading the same entity and matching the relevant property; on/off alone cannot confirm a mode, color, tilt or temperature change. Account for states such as a powered climate entity reporting `cool`, rather than `on`.

Use documented tolerances for quantized values. Limit readback retries; do not repeat a side-effecting POST after an uncertain receipt. Buttons, next-track and other actions without a reliable completion property should report acceptance, not verified completion. Cards and TTS must use the same distinction.

## Validate and deliver

Use the project's simulated-transport and frontend tests for the changed behavior, including wrong-device routing, missing capabilities, invalid values, failed readback and duplicate writes. Regenerate packaged frontend resources, source authorization hashes and embedded profiles when their sources change.

For hardware, use the user's current target and existing session authorization. Without a concrete authorized device/value, verify through mocks or read-only inspection instead of actuating arbitrary household equipment. Do not repeat broad physical tests when a focused check answers the question. Clearly distinguish source checks, firmware build/flash and actual device verification.

Before adding a new HA domain or feature, consult the relevant official HA entity/service documentation; feature bits and option attributes differ by domain.
