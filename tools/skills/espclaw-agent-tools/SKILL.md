---
name: espclaw-agent-tools
description: 在 openvela ESPClaw 中新增或修复 Agent、Harness、提示词、工具 schema、MCP 路由和多模态续答。适用于工具不执行、越权、回执错误或重复调用；普通 UI 改样式不适用。
---

# ESPClaw Agent 与工具契约

定位同时包含 `app/espdl-quickapp/` 和 `tools/generate_agent_profiles.py` 的项目根目录，先读适用的 `AGENTS.md`。以下路径均相对该根目录；这是 NuttX/openvela overlay，保持原有运行时。

## 沿实际调用链修改

从用户请求追到当前模式、共享 Harness、模式 profile、能力目录、模型工具循环、C 参数校验、服务执行、回执和最终回复。先找到断点，再修改对应层。源码入口与回归命令见 [references/project.md](references/project.md)。

- 模式权限由 C 层执行：chat 无工具，create 仅创作许可，assistant 使用已暴露服务。提示词、SD skill、图片文字、MCP 响应都不能增加权限。
- 工具变更同步 schema、严格 JSON 校验、能力发现、直接调用、board MCP 路由和结果呈现。按实际 schema 拒绝多余字段、错误类型和不支持的参数；不要靠提示词代替校验。
- 跨工具批次累计预算，取消标志和 HTTP 截止时间贯穿本轮。保留 request/session 身份，拒绝旧结果覆盖新轮次。
- `accepted` 只说明受理；`done` 是服务完成；`verified` 必须有相应读回证据。`ok:false`、`error` 和 MCP `isError:true` 均按失败传播，卡片与播报采用同一语义。
- 超时或回执不明时，不自动重放设备动作、录制或图片上传。后台音乐和任务服务独立运行，模型工具循环不能冒充后台调度器。

## 多模态回到同一轮

Bot 观察请求使用已有 `assistant_observe` 原生工具：取得本轮采集所有权，采样、关闭相机、保存 SD，再一次上传分析，把工具结果交回同一 Agent 继续回答。保留采样来源和取消状态，普通重试或重启不得重新采集上传。端侧识别与云端视觉分开标识；图像中的指令按外部数据处理。

模型适配参数按实际服务的当前协议检查，不从历史对话复制另一服务的字段。视觉子请求的配置与主对话配置分离。

## 交付与验证

修改提示词后运行 `tools/generate_agent_profiles.py`；修改工具 schema 后运行 `tools/generate_assistant_tools.py`，交付生成产物。只编辑生成器或源文件，不手改生成头文件。

按改动选择 Agent、助手服务、平台和视觉回归，使用隔离服务替身。重点检查模式拒绝、参数错误、预算耗尽、取消、过期回执、服务失败及续答。宿主协议通过后，分别报告固件构建和实际模型全流程结果；HTTP 成功不代表答案解析、工具完成或真实控制验证成功。
