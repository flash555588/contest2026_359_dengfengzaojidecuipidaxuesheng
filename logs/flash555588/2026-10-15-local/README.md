# 本机 AI 编码会话与编译测试记录

按用户 2026-10-15 的明确要求手动归集，范围为本项目的三个 Codex 会话和本次代码提交涉及的编译、主机测试、历史烧录记录。包含 2026-10-12 起的较早累积材料。入口为 [manifest.json](manifest.json)，会话清单同时维护在 [上级 manifest.json](../manifest.json)。原始文件继续保留在本机。

## AI 会话来源

| 会话 | 工作范围 | 分段来源 |
| --- | --- | --- |
| `01a13068-e466-7580-9273-24720cdd2ff0` | 网络、媒体与板端调试 | 3 个本机 rollout 文件 |
| `01a13476-90fb-7e90-919f-cb755ae031b5` | 桌面、Grok、HA 卡片、配置与性能 | 7 个本机 rollout 文件 |
| `01a13cd3-24e8-7e43-9e21-885adf415700` | 统一卡片、场景、日志及 PR 归集 | 当前会话快照 |

按会话合并分段，保留原始事件顺序、时间戳、用户/助手文本、工具调用与文本结果。每条导出事件附带 `source_file` 和 `source_line`；清单记录源文件字节数与 SHA256、导出文件 SHA256 和事件数。活动会话只覆盖清单 `exported_at` 前读取到的快照，后续对话可另行补导。

系统/开发者提示、模型推理、二进制图片和音频不导出。单条脱敏文本超过 200,000 字符时保留前段并写入 `export_truncation`，本机原始材料仍完整保留。未导出其他项目或仅因使用相同目录而与本次工作无关的会话。

当前 Windows 目录没有 `.repo/`，自动归集器不会采集。本次依据用户明确指令进行项目限定的手动导出，沿用比赛 JSONL 字段和清单约定；不伪造自动采集状态，也不创建假的 `.repo/`。

## 验证结果与适用范围

| 记录 | 实际结果 | 适用范围 |
| --- | --- | --- |
| `frontend-tests.log` | HA 70 项、门户 12 项通过 | 独立 PR 检出目录的前端逻辑 |
| `ha-template-tests.log` | 2 项通过 | 真实 Jinja 模板执行与边界 |
| `network-tests.log` | 续租/重连、默认 IPv4 网卡、SDIO 批处理通过 | 主机 ASan/UBSan；长期实网稳定性待测 |
| `runtime-tests.log` | QuickJS 执行预算、HTTPS 总期限/EINTR/取消通过 | 主机执行真实生产路径 |
| `ha-service-tests.log` | 3 组原生服务测试通过 | 门户会话、配置、类型化 HA 服务与传输 |
| `media-tests.log` | 直播 FLV 与 MP4 功能断言通过 | 现有 Helix AAC 仍有 UBSan 有符号算术告警，不能视为 sanitizer 全通过 |
| `grok-frontend.log` | 4 配置 × 32 状态通过 | SD 动画控制器、精灵复用、交互与暂停 |
| `grok-native-tests.log` | 160 个调色板/状态检查、24 次读取中销毁等通过 | 原生播放器与状态归约；离线 SD 文件 |
| `grok-offline-svg-tests.log` | 8 配置 × 32 状态通过 | 保留的离线 SVG 引擎与适配器 |
| `desktop-build.log` | PR 桌面中间分支完整编译成功 | SHA256 `6ad25fb0809b63634181ebf52e2c83f35dbcb71bce60d96c984ea0e9fbedc8a2`；此次未烧录 |
| `ha-scene-sync-20261015/` | 最终集成编译、8 张原生导航画面和嵌入源码一致性检查通过 | 固件 SHA256 `0806851c2d4e199d60702d6f5f3088408dd68f2f26fd3a768dfbd2327ca3f433`；未烧录、未做真实 HA 场景联调 |
| `ha-unified-firmware-20261015/` | 更早统一卡片版本烧录写入哈希校验通过 | 被动串口读取为 0 字节，未观察启动；手动触屏验证待用户完成 |
| `ha-config-fix-20261014/` | 历史配置修复与设备连接验证 | 仅适用于该历史固件，不外推到新场景代码 |
| `integrated-flash-20261014/` | 历史集成烧录、SD 资源校验与原生测试 | 具体固件和资源校验值见原始结果的脱敏副本 |

`evidence/` 保留来源目录，文件名在表中省略了该前缀。固件、SD 大型资源、缓存、模型分区备份和无关系统日志未加入本 PR。记录中的失败尝试与已知告警保留，不能用命令退出码替代实测结论。

## 脱敏与复核

导出时移除访问令牌、Bearer 值、密钥、密码/SSID 字面量、配对码、IP/MAC 地址、邮箱和本机用户路径；部分测试占位值也会被保守替换。`[REDACTED:类别]` 表示内容经过替换。二进制媒体使用省略标记；超长不透明值也做保守替换。导出清单记录替换次数，导出副本与原件哈希分别保存。

本次三个 JSONL 的必需字段、事件连续性、清单计数和导出 SHA256 已校验。日志反映实际执行记录，测试输出与 AI 会话分目录保存。仓库执行 `python3 tools/check_submission.py` 可复核既有会话清单格式。

## 对应代码 PR

依赖顺序：[媒体与 PPA #13](https://github.com/open-vela/contest2026_359_dengfengzaojidecuipidaxuesheng/pull/13) → [网络续租 #14](https://github.com/open-vela/contest2026_359_dengfengzaojidecuipidaxuesheng/pull/14) → [桌面、运行时与 Grok #15](https://github.com/open-vela/contest2026_359_dengfengzaojidecuipidaxuesheng/pull/15) → [配置门户与 HA 服务 #16](https://github.com/open-vela/contest2026_359_dengfengzaojidecuipidaxuesheng/pull/16) → [59 类卡片与场景 #17](https://github.com/open-vela/contest2026_359_dengfengzaojidecuipidaxuesheng/pull/17)。日志 PR 可独立审阅。

## 2026-10-21 后续补交

后续会话和格式修正见 [本次补交说明](../2026-10-21-local/README.md)。当前文件的计数和摘要已同步；本目录清单的 original_snapshot_sessions 保留10月15日原快照摘要，原 source_integrity 仍描述当时读取的源文件。
