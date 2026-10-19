# Project integration map

Find the repository root containing `app/espdl-quickapp/overlay/apps/system/desktop`. These paths describe that project's HA bridge, not an official Xiaomi account client.

| Concern | Source |
| --- | --- |
| Detail page, normalization, parameter controls and UI verification | `app/espdl-quickapp/overlay/apps/system/desktop/homeassistant/app.js` |
| Typed single-entity service whitelist | `app/espdl-quickapp/overlay/apps/system/hass/hass_actions.h` |
| Shared permission/configuration/cache/transport service | `app/espdl-quickapp/overlay/apps/system/hass/hass_service.c` |
| Fixed read-only device registry template | `app/espdl-quickapp/overlay/apps/system/hass/hass_devices_template.h` |
| AI tools, exact entity routing and result cards | `app/espdl-quickapp/overlay/apps/system/desktop/glass_board_home.c` |
| AI capability projection and fresh-state parameter checks | `app/espdl-quickapp/overlay/apps/system/desktop/glass_home_capabilities.inc` |
| Assistant profile and mandatory current-request constraints | `app/espdl-quickapp/overlay/apps/system/desktop/agents/assistant/agent.md`, `app/espdl-quickapp/overlay/apps/system/espclaw/port/espclaw_qpk.c` |

Read current source instead of assuming the mapping has not changed. Keep builds browser-free when that remains the user's chosen architecture; the HA page here runs through the existing native QuickJS/LVGL runtime.

## Tools available to the board AI

- `home_list`: read-only entity search; reconcile candidates with the user's exact target.
- `home_device`: read-only, paginated functions grouped by real device registry membership. Each member preserves its own entity ID, attributes and registered actions.
- `home_state`: fresh state and capability metadata for one exact entity. A local capability table alone is not confirmation that a service exists on the server.
- `home_light`: the existing fused unique-light path; preserve its single write and readback behavior.
- `home_service`: one typed service on one exact entity; stateful subfunctions use fresh capability preflight and property-specific readback.

Tool replies are bounded by `AGENT_TOOL_BYTES`; page large function collections rather than truncating JSON or declaring a partial option list complete. Read-only discovery must not require enabling AI control. A denied write remains denied.

## Source generation

From the repository root:

```text
python tools/generate_homeassistant_ui.py
python tools/generate_agent_profiles.py
```

The HA generator synchronizes the packaged JS, compiled resource and `hass_ui_auth.h` hash. Do not hand-edit only the generated copy.

## Focused checks

```text
python tools/test_home_functions.py --apps <prepared-apps> --output <new-output-directory>
node --test --test-isolation=none --test-name-pattern="device internal" app/espdl-quickapp/tests/homeassistant_frontend.test.cjs
```

`test_home_functions.py` uses the production AI/shared-service path with a scripted HA transport; it does not operate real devices. Broaden the existing frontend/service checks when a change affects older controls. Use supported options from the installed Node version.

The firmware wrapper is `tools/build_bilibili_v3.py`. Reuse the current prepared workspace, toolchain, baseline and audio inputs from a verified project build script. Check generated build metadata, reserved data offset/size and internal BSS before flashing. Discover the serial port; preserve user data and never assume another board has the same layout.

## Official capability references

- [Fan constants](https://github.com/home-assistant/core/blob/dev/homeassistant/components/fan/const.py)
- [Cover constants](https://github.com/home-assistant/core/blob/dev/homeassistant/components/cover/const.py)
- [Climate constants](https://github.com/home-assistant/core/blob/dev/homeassistant/components/climate/const.py)
- [Media player constants](https://github.com/home-assistant/core/blob/dev/homeassistant/components/media_player/const.py)
- [Water heater constants](https://github.com/home-assistant/core/blob/dev/homeassistant/components/water_heater/const.py)

Verify the version used by the actual HA installation when compatibility matters. In particular, fan and climate power flags differ from media-player flags; `PLAY` and `PLAY_MEDIA` are different capabilities.
