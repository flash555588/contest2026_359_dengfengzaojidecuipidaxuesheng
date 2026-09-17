# Home Assistant 原生智能家居

本文说明 2026-09-17 合入的 Home Assistant（HASS）原生集成。它不是小米官方米家
客户端，也不登录小米账号：需要先把米家等设备接入用户自己的 Home Assistant，再由
本固件通过局域网 HTTP 直连。

## 入口与界面

- 桌面“智能家居”卡片、Dock 家居按钮、全部应用列表和 `desktop ui ha` 都进入原生
  LVGL 页面（`overlay/apps/system/desktop/glass_hass_ui.inc`）。
- `desktop ui ha-settings` / `desktop ui ha-address` 直接打开 Portal 的智能家居配置页。
- `desktop ui ha-advanced` 打开 QPK 兼容界面
  （`overlay/apps/system/desktop/homeassistant/`，与 `quickapp/homeassistant/` 同源）。
- 未启用 `CONFIG_SYSTEM_HASS` 时，上述入口回退到 QPK 兼容界面，不提供原生页面。

## 源码结构

- `overlay/apps/system/hass/`：共享异步 HASS 服务与 HTTP 传输层
  （`hass_main.c`、`hass_service.c`、`hass_transport.c`），由 `CONFIG_SYSTEM_HASS=y` 启用。
- `overlay/apps/system/desktop/hass_portal.c`：Portal 配置读写与授权校验。
- `overlay/apps/system/desktop/hass_qjs.c`：QuickJS 兼容桥，供 QPK 侧调用同一服务。
- `overlay/apps/system/desktop/hass_ui_auth.h`：由 `homeassistant/app.js` 生成的
  SHA-256 授权绑定，必须与同份 JS 一起重新生成。
- `overlay/apps/system/desktop/glass_portal_config.c`：智能家居配置的
  `portal_ha_value()` / `portal_ha_save()` 持久化，不向 HTTP 回传令牌。
- `overlay/apps/system/desktop/glass_portal_ui.inc`：Portal 返回路径按来源区分
  “返回家居 / 返回设置 / 返回聊天”。
- `app/homeassistant/`：随固件内置的根级 HASS 应用与宿主回归测试。

## 能力边界

- 原生页面通过 `hass_open(HASS_READ | HASS_CONTROL)` 读取 `states` 缓存，状态有效期
  10 秒；控制只走 `hass_control()`，且仅接受 `light`、`switch`、`input_boolean`、
  `fan`，UI 层没有放宽服务白名单。
- 配置只允许设备 Portal 中显式授权的局域网 HTTP 地址；不支持公网 URL、任意子路径
  和 HTTPS。HTTP 会以明文传输令牌，只应在可信网络使用。
- 401/403 会清除已保存令牌，需要重新在 Portal 配置。
- 仓库不保存 Home Assistant 地址、长期访问令牌或设备 `/data`。

## 验证

- 宿主回归：`node app/homeassistant/tests/homeassistant.test.cjs`（5/5 通过）。
- 构建：重新生成 `homeassistant_resource.c` 和 `hass_ui_auth.h`，启用
  `CONFIG_SYSTEM_HASS=y`，执行完整 `make olddefconfig/context` 后再做一次干净的
  ILP32F 构建，不混用旧对象。
- 实板证据：`evidence/firmware-validation.json`、
  `evidence/mijia-ha-0.7.0-runtime.json`、`evidence/mijia-ha-0.7.0-ui-audit.json`、
  `evidence/ha-native-refresh-icon-boot-20260917.json` 与对应的
  `evidence/flash-ha-native-*-20260917.log`。
- 2026-09-17 的最终镜像为 7028888 bytes（SHA-256
  `6d3139f33c3ac95458620d3b9937eb2e380c0371541fb68263a9fb38654c59cf`），已通过
  8 MiB 程序分区检查；同日 esptool 写入与 “Hash of data verified” 记录见
  `evidence/mijia-ha-0.7.0-flash.log`。整机镜像本身不随源码提交，因为它包含无关
  系统模块与私有配置。
