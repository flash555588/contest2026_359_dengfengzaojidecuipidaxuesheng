# 桌面控件排版修复版

适用于当前 ESP32-P4 v3.2、1024×600 屏幕和 ESP32-C6 无线协处理器。基于已验证的 `app-storage-fix` 构建，配置完全一致，保留此前的启动黑屏、Wi-Fi DHCP 和应用存储修复。

## 修复内容

- 控制面板按“屏幕显示、个性化、网络与连接、系统管理”重新布局，统一行距、边距和按钮宽度。亮度条改为细轨道与圆形滑块，扩大触摸区域。
- 当前配置未启用 PWM 背光控制，亮度显示为 100%，滑条禁用并注明“当前屏幕使用固定亮度”。此包没有实现硬件调光。
- Wi-Fi 输入框及占位文字显式使用中文字体，修复“密码”显示方块；网络列表、输入区、连接按钮和键盘各自保留空间。
- 文件管理列表文字左对齐，统一输入框、按钮、圆角及键盘样式；深浅主题文字保持可读。
- 桌面传感器数值与温湿度分列；Hello 启动次数、按钮和计时文字分行；2048 分数与最高分使用独立列，避免数字增长后挤在一起。
- 提示弹窗正文可滚动，确认按钮拥有独立区域；弹窗位于顶层，避免被下拉列表覆盖。修复浅色主题 Toast 白底白字的问题，并限制长提示的宽度。
- 快应用内容层明确继承桌面背景色，修复透明层默认颜色导致 HA 和 Hello 在深色桌面中仍使用浅色内容的问题。
- HA 页面补齐空状态、主题颜色和标签宽度。QPK 事件槽从 16 增至有界的 32，使内置 HA 的 24 个按钮可以完整创建；按钮更换背景时同步调整文字颜色。内嵌 JS 明确以 NUL 结尾，传给 QuickJS 的长度不包含末尾 NUL。

## 固件与烧录

- 固件：`nuttx.bin`，对应调试文件 `nuttx.elf` 和 `nuttx.map`。
- SHA-256：`038b09a89f4a33e14fce5b8227cf373c63d2fb58705d363a4169464ce1765090`
- 大小：3,930,000 字节；烧录偏移：`0x2000`。
- 烧录擦除范围止于 `0x3c2000`，`/data` 从 `0x400000` 开始。本轮没有擦除或格式化数据分区。
- `resolved.config` 与存储修复版逐字节一致。

在源码包根目录执行 `python diagnostics/flash_ui_fix.py`，脚本会校验固件哈希、验证报告和 COM23 上的设备身份。已有同名烧录日志时会拒绝覆盖，应先归档日志。脚本烧录后停留在下载模式；复位设备即可启动。

## 验证记录

最终结果以 `evidence/hardware-validation.json` 为准；其中逐项列出实际截图覆盖的页面和状态。`evidence/review-light` 与 `evidence/review-dark` 保存最终固件的实板帧缓冲 PNG、LVGL 对象坐标和布局审计结果。截图来自板上正在显示的 RGB565 帧缓冲，未重绘或生成模拟界面。

`evidence/regression.json` 保存应用存储自测及 DHCP 回归。证据来源更正：`evidence/running-state.txt` 和 `evidence/final-console.json` 曾误从旧存储版复制，不能用于确认本版截图检查后的运行状态；元数据中的对应结论已置为未验证，详见 `evidence/provenance-correction.json`。本版的烧录、启动、回归和 `review-light` / `review-dark` 截图仍为本版实测记录。`attempt*`、`screenshots*`、`final-screenshots` 等目录为历史调试记录，不代表最终固件的通过结果。

布局检查排除父子包含和有意的模态遮罩覆盖，检查已采集状态中可见文字、按钮、输入框、开关、滑条等的非预期重叠与越界，并人工查看实际截图。它不等同于物理触摸精度测试，也不覆盖任意第三方 QPK 或未加载的动态内容。HA 尚未配置服务端实体；蓝牙配对、音乐播放及断电恢复不属于本轮通过项。相机未注册 `/dev/video0`，本轮只检查了启动失败提示及按钮布局，没有验证实时取景。

## 源码与复现

- `overlay/`：本版实际使用的源码覆盖层，包含此前功能修复。
- `ui-layout.patch`：相对于已验证存储版的桌面 UI 差异。
- `full-fix.patch`：相对于 `current-build-tree-source.tar.gz` 的完整覆盖层差异。
- `homeassistant-app.js`：可读的 HA 页面源码，与生成的 C 资源对应。
- `build-metadata.json`：构建、配置和硬件验证信息；`SHA256SUMS`：交付文件哈希。

构建使用隔离目录 `/tmp/v3-desktop-ui-layout-20260914`，原版 `/home/streetartist/nuttxspace` 保留。编译器为 Espressif GCC 14.2，运行库沿用 xPack 的 RV32IMAC/ILP32 软浮点 libgcc。

在完整工作区根目录运行 `diagnostics/update_ui_layout.py` 生成 UI 覆盖层，再从 WSL 运行 `diagnostics/build_ui_fix.py`。该流程依赖工作区内的 `diagnostics/ui-baseline`、已验证基础构建树和工具链；包内 `build-scripts/` 保存同一批脚本供追溯。

本版保留 `desktop ui <页面>`、`audit`、`frame`、`frame-free` 诊断命令。NSH 只提交请求，由桌面线程操作 LVGL；JTAG 只读截图。布局日志不记录输入框内容或密码；截图缓冲按需分配，`frame-free` 释放。主题诊断命令只改内存中的显示状态，不保存用户偏好。
