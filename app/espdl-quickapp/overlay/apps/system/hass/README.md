# 原生共享 Home Assistant 服务

本目录提供进程内共享服务与 HTTP 传输，不是 Home Assistant 服务端、IPC 守护进程或
独立凭据保险库。完整集成说明见 [HASS.md](../../../../HASS.md)。

## 文件与启用条件

`hass_service.c/.h` 提供共享状态与请求接口，`hass_transport.c/.h` 实现传输，
`hass_main.c` 提供诊断入口；Make/Kconfig/CMake 文件负责构建接入。
`CONFIG_SYSTEM_HASS` 依赖 `BUILD_FLAT`、`NET_TCP` 及 pthread 支持，不能只复制 C 文件就认为已启用。

桌面 UI、Portal 与 QuickJS 桥位于相邻的 `desktop/`，不是本目录的一部分。
传输仅支持受限局域网 HTTP，不提供 HTTPS；令牌会明文发送，必须按集成文档限制网络环境。

## 来源与测试

[SOURCE-MANIFEST.json](SOURCE-MANIFEST.json)保存一组源文件摘要及当时的验证范围，
不能把该清单当作整个固件的签名或最新实板结论。修改列出的源文件后须同步复核清单。

根级宿主回归入口为 `node --test app/homeassistant/tests/homeassistant.test.cjs`
（从仓库根目录执行）。完整构建和硬件证据需要另行确认。保留现有 SPDX、版权与许可声明。
