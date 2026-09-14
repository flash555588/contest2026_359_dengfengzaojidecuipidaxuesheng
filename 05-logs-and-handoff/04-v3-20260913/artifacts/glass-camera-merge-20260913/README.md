# 附件相机代码合并记录 · 2026-09-13

已将附件的相机 Quick App 合入当前 Glass 桌面，并生成新的 v3 离线候选。
只处理相机范围；附件的说明和脚本作为来源资料，未执行附件中的批量覆盖脚本。

## 合入内容

附件 `app.js` 与 `manifest.json` 对应 `com.openvela.camera.preview`，已复制到
`camera-app/glass-desktop/camera/`。保留取景/重试、停止、帧计数和拍照占位提示，
删除重复且不可达的 `if (running)` 分支和无用途的 `mode` 变量。
清单保持原样。原 `camera-app/camera/` 中的历史副本未覆盖。

新增 `tools/generate_camera_resource.py`，沿用工程已有生成器格式，生成
`camera_resource.c`。联合构建现在显式复制、哈希记录和链接检查该资源；
构建开始前运行 `--check`，拒绝源码与生成资源不一致的情况。
由此替换此前从 WSL 基线继承、带“空白基线”按钮的旧诊断界面资源。
原生 `system.camera.blank()` API 保留，未用附件的旧运行时覆盖当前实现。

## 底层补丁核对

附件 0004 与工程已有 0004 的字节哈希相同。对当前 WSL 内核逐文件执行
反向应用检查，11 个文件中 9 个匹配：CSI Kconfig、CSI C/H、DSI C/H、
HAL 构建清单、板级相机注册、SC2336 和 V4L2 采集实现。
这证明这些相机改动已经存在，无需重复应用。

两个有后续差异的文件是 desktop-v1/defconfig 和 esp32p4_lcd.c。
附件 defconfig 还包含 Home Assistant 启动入口及网络配置，不适合覆盖当前 v3 配置；
LCD 的三页数量、fblen、yres_virtual 和页偏移逻辑已在当前树中，保留现有后续修复。
附件 0005 的相机桥接入口也已存在，同时夹带大量 Home Assistant 和旧桌面改动，
因此按功能核对后仅更新所缺的相机应用资源，没有整体套用该补丁。

附件 SHA-256 均与提供的 SHA256SUMS.txt 一致：

```text
0004-esp32p4-camera-rgb565-preview.patch  80c36ade93c08e6b6079a2daaba8864d55d3312cea8c7993598ff5f4f7d8b483
0005-esp32p4-desktop-camera-preview.patch 1895984ea0f3f895f0a15ca9718aa6080dee270350d8201aecfca21f6833499c
app.js                                 91e2f33367d719f7c97462ad09ff109663db895cfebabc7b80823f7a88005fe0
manifest.json                          c0face45175933085a37eca14fd7d7443452bc6a62c1f21692e74f27cc85e181
```

## 验证与固件

普通构建与 Address/Leak/UndefinedBehavior Sanitizer 构建各通过 26 项真实
QuickJS/LVGL 测试，包括原有 7 项生命周期测试、18 项相机测试和新增的完整应用资源测试。
新增测试直接执行生成资源，覆盖失败重试、重复启动、拍照占位、关闭设备延迟时停止及再次启动。
构建脚本 18 项测试通过，九个生产 C 单元通过 RISC-V 编译。

Sanitizer 最终使用宿主 `-O1` 构建。最初无优化 Debug 构建触发完整 JS 用例的
QuickJS 栈上限，日志保留在 tests/asan-debug-stack-limit.log。
优化编译还暴露了原测试头文件宏被 glibc 重定向绕过的问题，现已使用链接级
open、poll 与 __poll_chk 替换，确保模拟设备调用路径有效。生产 JS 栈预算未改。

[核验记录](VERIFICATION.json) 确认 22 个生产输入与源码和 WSL 构建树一致，
上一相机异步退出候选的 21 个生产输入及配置全部保持一致，仅增加显式相机资源输入。
合入后的 JS 字节已在最终二进制中找到，异步清理和相机桥接符号完整。

[v3 固件](v3/nuttx.bin) 大小 3,830,076 字节，SHA-256：
`dd04499d2e0f4238374adc4d97f8325a4375f6d2c04e7442d78d0b8e95e045ab`。
现有 0x2000 布局下结束于 0x3a913c，距离 /data 分区剩余 356,036 字节。
完整 ELF、map、配置、元数据和构建日志在 v3 目录，测试证据在 tests 目录。
WSL 构建树仍为 `/home/flash/glass-desktop-resources-20260913`，基线和工具链沿用
[上一份相机交接](../glass-camera-cancel-20260913/README.md)。未刷机，未生成新 v1 固件。

在工程对应的 WSL 目录重新生成资源及核验：

```bash
python3 -B camera-app/glass-desktop/tools/generate_camera_resource.py
python3 -B camera-app/glass-desktop/tools/generate_camera_resource.py --check
python3 -B artifacts/glass-camera-merge-20260913/verify_candidate.py
```

## 保留的功能边界

“拍照”在附件中就只显示提示，没有图像编码或照片写入实现，本次没有将它描述为已实现功能。
当前全屏零拷贝预览仍暂停 LVGL 绘制及定时器；资源中的按钮和帧计数不代表实板预览中
已经支持持续交互叠加层。测试通过直接派发事件验证绑定，不等同于真实触摸验收。
实板可沿用 NSH 的 `desktop camera-stop` 停止，`desktop camera` 重开。
CSI 延迟停流、显示页交还、真实触摸交互和长期稳定性仍需后续实板验证。
