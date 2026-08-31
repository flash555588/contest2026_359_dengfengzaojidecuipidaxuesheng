# LVGL installation record

- Upstream: https://github.com/lvgl/lvgl
- Release: v9.2.2
- Archive SHA-256: `B3D33F7AAD1360F588762208D0F563574392DEBBC28D9187BDA86012909F2203`
- Installed tree: `/home/flash/openvela-contest359-release/apps/graphics/lvgl/lvgl`

The official `src`, `demos`, `examples`, `env_support`, public headers, and
root build metadata were installed from the tagged release. OpenVela's local
`Kconfig` extensions and `lvgl.mk` are retained because they provide the
NuttX framebuffer, touchscreen, and generated configuration integration.

The installed release includes the v9.2.2 NuttX LCD cleanup fix that frees
the draw-buffer data rather than the draw-buffer descriptor.
