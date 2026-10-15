# Home Assistant 原生智能家居

本文说明 2026-09-17 合入的 Home Assistant（HASS）原生集成。它不是小米官方米家
客户端，也不登录小米账号：需要先把米家等设备接入用户自己的 Home Assistant，再由
本固件通过用户配置的 HTTP 地址直连。

## 入口与界面

- 桌面“智能家居”卡片、Dock 家居按钮、全部应用列表和 `desktop ui ha` 都启动内置
  QuickJS `homeassistant/app.js`（与 ZIP 提供的 `app.js` 同源），通过共享的
  `system.homeAssistantService` 访问现有 Home Assistant 连接；旧的
  `glass_hass_ui.inc` 保留为原生回归/参考实现。
- `desktop ui ha-settings` / `desktop ui ha-address` 直接打开 Portal 的智能家居配置页。
- 未启用 `CONFIG_SYSTEM_HASS` 时，QuickJS 应用仍可启动，但不会获得原生服务授权；
  正式连接和控制验证仍要求启用该配置。

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
- `app/homeassistant/`：HASS 原生服务与页面的宿主回归测试。

## 能力边界

- QuickJS 页面通过共享 `hass_open(HASS_READ | HASS_CONTROL)` 服务读取 `states`
  缓存，状态有效期 10 秒，并每 10 秒自动同步；控制只走 `hass_control()`，且仅
  接受 `light`、`switch`、`input_boolean`、`fan`，UI 层没有放宽服务白名单。
- 控制返回 HTTP 2xx 后不会乐观改写卡片。原生页在 8 秒内最多 4 次读取原实体，
  开关状态一致且亮度误差不超过 1 个百分点才显示确认成功；否则明确提示结果未确认，
  不自动重发控制命令。没有 `brightness` 属性的灯不显示亮度滑杆。
- 原生页按 2×3 卡片分页展示，支持全屋、常用、环境分类和扩展的只读 HA 实体域；
  一页 6 个网格位置，大小卡片混排、自动分页。最多保存 512 个状态实体，超出时
  明确提示截断。大小选择最多保存 128 项，按服务端和设备 ID 隔离。
- 底栏只保留设备控制、刷新和连接设置；旧 QPK 高级管理页不进入原生固件构建。
  HTTP 接收从 16 KiB 按需增长，JSON 响应上限 256 KiB（额外预留 4 KiB HTTP
  头/分块开销）。超过上限时提示使用关注同步，不会默默丢弃响应。
- 配置接受设备 Portal 中显式授权的 `http://主机[:端口]`，包括公网 IPv4 地址；不支持
  任意子路径和 HTTPS。HTTP 会以明文传输令牌，应自行确保链路可信。
- 401/403 会清除当前连接的内存令牌；持久化配置保留，Portal 仍可修改令牌。
- 仓库不保存 Home Assistant 地址、长期访问令牌或设备 `/data`。

## 远程配置更新

- Portal、设备配置桥和启动恢复共用原生地址/令牌校验：URL 最多 127 字节，令牌
  最多 511 个 ASCII 可见字符；端口写作 `:8123`，不接受 `/8123` 子路径。
- 合并补丁后先校验、再保存、再更新共享服务；全过程串行，避免混用两次修改的
  地址和令牌。URL 单独更新保留现有令牌；存储失败保留原连接。
- 更新成功取消旧请求、清除旧状态缓存并递增配置版本；已打开的 QuickJS 家庭页面
  发现版本变化后清除旧家庭数据并重新同步。同一地址更换令牌也触发更新。
- 启动恢复和应用入口读取/应用使用同一个配置锁；完全相同的配置不会重复取消连接。

## 设备与多实体卡片

- 共享服务的 `get("devices")` 只运行固件内固定的只读模板，通过 HA 官方
  `/api/template` 和 `device_id` / `device_entities` 获取注册关联，不允许应用传入任意
  模板，不改变设备注册或 HA 配置。
- 同一设备 ID 的状态实体组合成一张卡片；名称相同不代表同一设备。没有设备关联时
  保持独立实体；关联查询不可用时仍能同步状态。关联每 5 分钟重新读取，换地址或
  更新令牌后立即重新读取。
- 主控制实体保持自己的 entity ID。温湿度等读数合并到同一设备的快捷卡片；多路开关
  的按钮分别指向自己的实体。“更多操作”把同设备功能直接融合到一张卡片，
  不再单独显示功能选择列；卡片内部分页访问其余控件，读数实体保持只读。
- 注册查询附带设备型号与厂商，已知 MIoT 型号优先决定产品品类；明确的提供者品类
  声明也保留。辅助指示灯、检测开关不再替换摄像机、门锁等设备的主卡片。
  缺少主控制实体时保持只读，更多操作仍指向每项功能原来的实体 ID。
- 大卡片亮度条及更多操作页亮度条都支持拖动；拖动更新显示，松手仅提交一次
  `brightness_pct`。滑条手势不触发翻页，拖动期间暂停自动刷新；取消或连接版本变化
  会丢弃未提交操作。后台返回目标状态后才确认控制成功。

## 更多功能与色温控制（2026-10-14）

- 兼容原 `control(entity, on, brightness)`，新增原生
  `callService(entity, service, bodyJSON)`；同一权限与异步通路，原生校验
  单一实体、服务、参数类型/范围，拒绝其他目标、重复字段和未登记服务。
- 色温采用 `light.turn_on` 的 `color_temp_kelvin`，按实体提供的 Kelvin 范围
  映射；兼容读取旧 mired 属性。亮度与色温拖动实时预览，松手提交一次；
  同一灯的两个参数独立排队，核对各自属性，取消拖动恢复显示。
- 更多功能按设备关联集中展示，支持 number/input_number 数值、select/input_select
  选项、button/input_button 执行、text/input_text 编辑，以及风扇、窗帘、空调、
  加湿器、播放器、扫地机、热水器与门锁的已登记操作。服务目录缺失或原生扩展
  不可用时不显示可写入口；传感器与不可用实体不发送控制。
- 操作型服务（如 button.press）受理后读取状态，不要求变成 on/off；不会自动重发。
  数值/模式等操作核对对应属性；未知控制结果清空后续操作，等待用户刷新。
- 更多功能使用蓝灰配色与蓝色数值条。色温条（大卡片/更多功能）全宽从暖黄渐变到白光，
  随实体 Kelvin 上下限设置端点；白色标记带描边，在白光区域也能分辨。
  采用 LVGL 原生渐变，拖动只更新标记与数值；首页参考灯具小卡样式保持不变。
  灯光更多功能沿用原版横向滑条，电源在右上，有灯光效果时在底部显示选项；
  数值/模式/动作等其他实体直接融合在同一卡片，控制与分页不切换设备。
- 新增验证：前端颜色范围、拖动与功能分组/选项/动作；原生 C 参数和权限检查；
  真实 LVGL/QuickJS 指针滑动、色温回读和功能选择。宿主模拟不等同于设备联动测试。

参数依据：[Light](https://www.home-assistant.io/integrations/light/)、
[Number](https://www.home-assistant.io/integrations/number/)、
[Select](https://www.home-assistant.io/integrations/select/)。

官方依据：[REST API](https://developers.home-assistant.io/docs/api/rest/)、
[设备模板函数](https://www.home-assistant.io/template-functions/#devices)。

## 验证

- 宿主回归：`node app/homeassistant/tests/homeassistant.test.cjs`。
- 构建：重新生成 `homeassistant_resource.c` 和 `hass_ui_auth.h`，启用
  `CONFIG_SYSTEM_HASS=y`，执行完整 `make olddefconfig/context` 后再做一次干净的
  ILP32F 构建，不混用旧对象。
- 实板证据：`evidence/firmware-validation.json`、
  `evidence/mijia-ha-0.7.0-runtime.json`、`evidence/mijia-ha-0.7.0-ui-audit.json`、
  `evidence/ha-native-refresh-icon-boot-20260917.json` 与对应的
  `evidence/flash-ha-native-*-20260917.log`。
- 2026-09-17 的已烧录镜像为 7028888 bytes（SHA-256
  `6d3139f33c3ac95458620d3b9937eb2e380c0371541fb68263a9fb38654c59cf`），已通过
  8 MiB 程序分区检查；同日 esptool 写入与 “Hash of data verified” 记录见
  `evidence/mijia-ha-0.7.0-flash.log`。整机镜像本身不随源码提交，因为它包含无关
  系统模块与私有配置。

2026-09-18 的分页、扩展实体卡片与控制确认属于后续源码版，须以新的构建报告为准；
在重新烧录和实板控制前，不沿用 2026-09-17 的硬件通过结论。
当前源码版完整构建为 7038028 bytes（SHA-256
`5dea759c4a63753935bb1aaf4a29ea646a0631d1b14296e5914c31726e9f0ac7`），
SRAM 链接占用 469524 / 978880 bytes，程序分区、ILP32F ABI、关键符号和启动静态区
边界检查通过；`hardware_tested` 仍为 `false`。
