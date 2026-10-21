# ESPClaw Harness 框架

执行链为：用户完整请求 → 固定模式／独立历史 → 共享 Harness + 模式 profile + 当前能力 → 模型工具循环 → C 参数与权限校验 → 真实服务 → 受理／完成／验证回执 → 简短回复。

## 运行约束

| 项目 | 实现 |
| --- | --- |
| 角色 | `agents/HARNESS.md` 固件强制附加，随后是当前模式 profile、创作指导与能力目录 |
| 模式 | chat 不调用工具；create 仅草稿／技能；assistant 调用设备／服务／技能 |
| 循环 | assistant 最多 16 轮，create 最多 8 轮；聊天无工具 |
| 调用 | 每轮请求 assistant 64 次、create 32 次；跨多工具批次累计，达到上限返回 `tool_budget_exhausted` |
| 参数 | 扩展工具通过严格 JSON 和各工具 schema 检查；模式拒绝在调用前执行 |
| 回执 | `ok:false`、`error` 或 MCP `isError:true` 都是失败；卡片不能覆盖真实 verified |
| 取消 | 保留原子取消标志、HTTP 截止时间与核心销毁保护；超时退出不释放仍被核心使用的上下文 |
| 历史 | 保留模式和轮次，按当前模式隔离；本轮必须重新查询设备状态 |
| 后台 | 资料／任务持久化服务与音乐独立于聊天；模型工具循环不是后台调度器 |
| 图片 | 独立 `assistant_vision`，当前已保存相机回执、2 分钟有效期、显式 upload、每张仅一次尝试 |
| Bot 采集 | 用户提出观察问题后，助手调用原生工具 `assistant_observe`，约5秒保留3帧JPEG；保存SD并释放相机后一次上传，观察结果交回同一轮Agent继续回答；取消和失败不自动重录重传 |

预算在 `claw_qpk_session` 中累计；模式拒绝、取消与 JSON 解析失败不会执行工具。不能依赖模型主动遵守循环上限。达到上限后仍须如实报告待完成项。

## 提示词与外部数据

`tools/generate_agent_profiles.py` 将共享 Harness 与三个模式文件嵌入固件。SD profile 可以定制表达和专业知识；技能与 MCP 文本只能提供任务数据，不能获取未暴露的工具。相机图片内的文字／二维码同样视为场景数据。

视觉请求使用独立、无工具的 system prompt，只包含本次单张图片或 Bot 的3张时序采样和问题，不发送聊天历史或家居状态。端侧结果与云端分析分别标识。Bot 默认不启动人脸跟随；AI 入口先停止本页端侧任务并等待释放，采集不碰LCD、不做本地JPEG解码、不写Flash临时文件；相机关闭后上传，最后才保存结果文字到当前模式历史。camera_samples来源随历史持久化，普通重试拒绝该来源；重启也不恢复上传。观察不构成 verified 控制回执，不推测采样间未观察到的动作或音频。

## 验证

新Bot相机及语音轮次使用会话历史，独立观察文字写入SD的espclaw-observations目录；旧Flash历史不迁移。成功、失败与取消均跳过Flash快照；普通快照过滤新会话轮次，SD不可用只保留会话并提示。打开ESPClaw保持原模式，不取消交接中的相机任务。MiMo视觉子请求独立关闭默认思考，限额1024，输出截断返回明确失败，不改主对话参数。

在仓库根、Linux/WSL 与匹配准备树中运行：

```bash
python3 tools/generate_agent_profiles.py
python3 tools/generate_assistant_tools.py
python3 tools/test_agent.py --apps /path/to/prepared/apps --output /path/to/output/agent
python3 tools/test_assistant_service.py --apps /path/to/prepared/apps --output /path/to/output/service
python3 tools/test_assistant_platform.py --baseline /path/to/prepared --output /path/to/output/platform
python3 tools/test_vision.py --apps /path/to/prepared/apps --output /path/to/output/vision
python3 tools/test_board_interfaces.py --apps /path/to/prepared/apps --output /path/to/output/interfaces
python3 tools/test_ble_central.py --output /path/to/output/ble
python3 tools/test_usb_camera_pools.py --output /path/to/output/usb-pools
python3 tools/test_usb_interfaces.py --output /path/to/output/usb
python3 tools/test_interface_protocols.py --apps /path/to/prepared/apps --output /tmp/ifproto
```

使用 ASan/UBSan 检查真实路由、状态与协议代码；硬件、网络和模型边界使用确定性替身。视觉测试还独立解压 PNG、核对 CRC、像素顺序、RGB565、纵横比和图片负例。宿主测试不能证明真实传感器画质、模型识别质量、热插拔电气稳定性或 DMA 正确性。

Bot、全局唤醒和语音面板发起的新对话使用相同会话策略：请求前、失败、取消及完成均不写Flash历史，成功文字独立保存SD；相机观察为camera-<id>-<序号>.json，语音对话为dialog-<id>-<序号>.json。长答案分页文件为sd-reply-<id>-<序号>.txt，只从实际挂载SD读取，不回退Flash。旧历史不迁移，文字工作台保留原有持久化策略。此措施仍须真实AI全流程和实体屏幕验收。
