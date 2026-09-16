# 版本历史与目录边界

当前分支只保留大赛交付结构。旧版本由 Git 对象保存，不再用多个版本目录重复占用当前
文件树。

| 提交 | 用途 |
| --- | --- |
| `1c92d13` | 官方大赛基线：板级、芯片层、固件、补丁、作品文档和 AI Coding 日志 |
| `170a792` | 2026-09-15 本地源码集合完整导出，包含 `01-*` 至 `05-*` 和诊断资料 |
| `ff5c6bd` | 2026-09-17 最新完整开发集合，包含 ESP-DL、聊天、音乐、门户和硬件 API 开发资料 |

查看旧树而不改动当前工作区：

```bash
git ls-tree --name-only ff5c6bd
git show ff5c6bd:04-v3-20260913/espdl-quickapp/README.md
```

需要完整复现旧开发环境时，使用独立 worktree：

```bash
git worktree add ../contest-source-ff5c6bd ff5c6bd
```

当前交付入口如下：

- `app/`：应用源码、当前应用 overlay、模型和应用级验证证据。
- `board/`：ESP32-P4 Function-EV-Board 板级适配。
- `chip/`：ESP32-P4 架构与 Espressif 驱动适配。
- `firmware/`：经过记录的固件、校验值和测试报告。
- `quickapp/`、`ouo/`：快应用源码与运行时示例。
- `tools/`：构建、应用补丁、自检和烧录工具。
- `logs/`：符合大赛格式的 AI Coding 日志。
- `docs/`：作品说明、验证资料和本历史索引。

`nuttx`、`apps` 的公共仓修改继续以 `tools/patches/` 中的可复现补丁表达，不在主参赛仓
根目录保存公共仓的完整副本。
