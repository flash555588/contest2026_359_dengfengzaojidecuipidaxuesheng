# Host-only pinned pixel renderer

This snapshot preserves the approved procedural renderer for asset generation.
It is never compiled by the firmware Makefile. Run `render_grok_sd.py` on the
prepared native LVGL host cache to generate raw RGB565+A8 SD banks.

The bundled OuO resource contains the original MIT SDK; its license is retained
in that resource and `project/ouo/vendor/`. Production OuO is generated from the
small SD controller, rather than from this engine.
