# ESPClaw Skills 与 MCP

ESPClaw 与 Grok 语音小球共用模型配置及对话服务，按模式隔离提示文件、工具、输入草稿和上下文。两种界面切到同一模式时，可以继续同一段对话。配置变更从下一次请求生效，当前请求固定模式及配置。

## ESP 工作区界面

工作区使用左侧模式与当前对话导航、中央消息区和底部输入框。新任务中的小球与标题、快捷卡片、输入框共用中心线；开始对话后，原有小球缩为 48 px，保留在 AI 输出文字旁边，表情由真实请求状态驱动。滚动时小球留在输出区左侧，输入文字或阅读完整回复时暂时隐藏，收起后恢复。点击小球或「语音」可进入同模式的语音对话。

创作模式右上角「草稿」展开预览与安装面板；助手模式显示工具能力入口。面板不增加独立定时器，隐藏时不参与绘制，也不会运行应用脚本。点击「预览草稿」才启动真实 QPK 应用，预览完成后才能安装。当前原生回归覆盖面板连续开关 30 次的控件／定时器数量稳定、小球实例不变及流式消息原位更新；这些检查不能代替 ESP 实机帧率和堆内存测量。

## 三种模式

| 模式 | 默认入口 | 能力 | 默认提示文件 |
| --- | --- | --- | --- |
| 聊天 | Grok 语音小球 | 自然交流，不开放工具 | `overlay/apps/system/desktop/agents/chat/agent.md` |
| 创作 | ESPClaw、开发者 CLI | QPK 草稿工具和创作技能，先预览再手动保存 | `overlay/apps/system/desktop/agents/create/agent.md` |
| 助手 | 两个界面的模式选择器 | 助手技能、已配置和获准的 MCP 工具 | `overlay/apps/system/desktop/agents/assistant/agent.md` |

点击当前模式名称选择切换；语音会话或 AI 请求进行中须先结束。模式选择不会自动推断用户意图，也不会由模型自行切换。创作控件仅在创作模式显示。模式选择在当前运行期间分别记忆，重启恢复上述默认入口。

三个 `agent.md` 是设备 Agent 的运行提示文件，不是 Codex 仓库开发指令。默认内容由 `tools/generate_agent_profiles.py` 生成内嵌头文件，无 SD 卡也可运行。可选覆盖文件为 `/sdcard/espclaw/agents/{chat,create,assistant}/agent.md`，UTF-8、最多 8 KiB，支持 BOM／CRLF；缺失、格式无效、超限或符号链接时使用内置默认值。每次请求加载一次，修改只影响下次请求。提示文件、技能和 MCP 返回内容均不能扩大模式权限；工具桥接层对越权调用直接返回 `mode_denied`。

历史仍使用现有双槽日志文件，总计保留最近 6 轮以控制内存，每轮保存稳定的模式标记。模型只收到当前模式的已完成历史，界面只显示当前模式的记录。「新对话」只清空当前模式；旧版未标模式的历史归入创作模式。重试保持原模式，其他模式正在执行时不能发送、重试或清空它的请求。

## 配置入口

在设备上打开「AI 设置／扫码配置」，使用已有配对流程连接设备配置页，在「Agent 能力」设置本地技能和 3 个 MCP 服务槽位。

服务默认关闭。填写显示名称、HTTPS MCP 地址及可选 Bearer 令牌，然后启用。允许工具列表支持逗号分隔的精确工具名，例如 `weather_get,scene_list`；空列表只允许发现工具，`*` 表示允许该服务的所有已发现工具。令牌留空保留原值；勾选「清除已保存的访问令牌」可改用无令牌服务。配置读取接口只返回令牌是否已设置。

MCP 配置和技能索引在请求开始时读取，联网延迟到模型实际调用工具时。对模型开放 `mcp_list_tools`、`mcp_call_tool`；服务名称使用稳定别名 `mcp1`、`mcp2`、`mcp3`。模型先发现工具，获得原始 `inputSchema`，再按参数调用。调用同时受用户配置的工具允许列表约束，每个服务每轮最多保留 32 个已发现工具名。

## 自定义 Skill

通过配置页的「文件空间 → SD 卡」建立以下目录并上传 `SKILL.md`，也可以直接写入 SD 卡：

```text
/sdcard/espclaw/skills/assistant/weather/SKILL.md
/sdcard/espclaw/skills/create/my-app-style/SKILL.md
```

```markdown
---
name: weather
description: 用户询问天气时使用，优先调用已配置的天气工具。
---

1. 如果用户没有说明地点，先询问地点。
2. 使用 mcp_list_tools 查找可用天气工具及其 inputSchema。
3. 按实际参数调用 mcp_call_tool，依据工具结果回答。
4. 调用失败时明确说明，不能把猜测当成实时天气。
```

目录名是技能 ID，限 31 个 ASCII 字母、数字、短横线或下划线。文件使用 UTF-8，支持 BOM、LF 和 CRLF；每份最多 8 KiB。前言里的 `description` 使用一行文字，最多 160 个 UTF-8 字节。每轮最多索引 12 份自定义技能，目录扫描上限 64 项；符号链接、无效文件和超限文件不进入索引。全文通过 `skill_read` 按需读取，技能不执行脚本或系统命令。

助手模式内置 `conversation` 技能，创作模式内置 `quickapp` 技能，聊天模式不索引技能。自定义技能只能在所属模式目录内发现和读取。上一版 `/skills/技能名/SKILL.md` 应移到对应的 `create` 或 `assistant` 目录，避免自动把所有技能授予两个模式。QPK 工具仅在创作模式可用，已有大脚本参数上限不因扩展工具的上限而缩小。

## MCP 兼容范围

- Streamable HTTP：HTTPS POST、JSON-RPC、JSON 与 SSE 响应。
- 初始化版本首选 `2025-11-25`，接受 `2025-06-18`、`2025-03-26`；完成 `notifications/initialized` 后开始操作。
- 会话 ID 和协商版本用于后续请求，支持 `tools/list` 游标分页及 `tools/call`，保留 `content`、`structuredContent`、`isError` 等结果字段。
- 单次请求最多 15 秒，单个 JSON 消息／工具结果最多 16 KiB、嵌套深度最多 16。SSE 累计接收上限 128 KiB。
- 停止对话会中止当前请求，并尽力发送取消通知；服务端是否已完成操作取决于服务端，超时和会话过期不会自动重放工具调用。下次发现请求可建立新会话。
- 请求结束释放本轮扩展上下文，尽力发送有会话 ID 的 DELETE，单个关闭请求限 1 秒。
- 沿用固件的 TLS 信任链，手动配置 Bearer 令牌。当前未实现 OAuth 登录、stdio 子进程、旧版双端点 HTTP+SSE、资源／提示词能力，以及服务端 sampling／elicitation。连接这类服务需提供兼容的 MCP HTTP 网关。

协议参考：[MCP Streamable HTTP](https://modelcontextprotocol.io/specification/2025-11-25/basic/transports)、[生命周期](https://modelcontextprotocol.io/specification/2025-11-25/basic/lifecycle)、[工具](https://modelcontextprotocol.io/specification/2025-11-25/server/tools)。

## 验证

对话正文支持轻量 Markdown：标题、无序／有序列表、引用、分隔线、行内强调、删除线、行内代码与带语言／行号的围栏代码块。链接保留可读的目标地址；图片和 HTML 不执行、不自动下载。当前不是完整 CommonMark 实现：嵌套列表、表格布局、数学公式暂按文本保留。英文粗体使用 DejaVu ASCII 子集，中文沿用现有字库；斜体强调以下划线显示。字体共约 31 KiB，只创建一次，中文不重复打包。

思考／输出状态采用无底板的小行，展开后显示真实工具调用名称，最多保留最近 4 项和累计调用次数；不据调用推断执行成功，不显示模型内部思维。最近一轮完成后仍可查看这些本次会话的调用记录，重启后的持久历史不保存该临时过程。每条输出最多 24 个排版块、每块最多 64 个行内片段，超出部分保留原文。流式更新复用已有控件，代码横向滚动，状态动效复用现有刷新定时器并遵守减少动态效果设置。

`tools/test_agent.py --apps <prepared-apps> --output <test-output>` 使用 ASan／UBSan 执行真实技能、配置、JSON-RPC、字节分片 SSE 解析、ESPClaw 工具桥接、对话线程及双槽存储；覆盖跨模式工具拒绝、上下文隔离、旧历史迁移、模式标记持久化、局部清空、重试和请求模式固定。HTTP 对端和模型调用由确定性测试替身提供。配置页另有 `portal_frontend.test.cjs`，原生 LVGL 界面和连续语音流程由 `render_grok_conversation.c` 验证。

尚未配置用户的实际 MCP 地址和令牌；本机测试不代表实机网络、第三方服务或模型端已验证。固件构建结果和烧录状态见本次输出目录的 `verification.json`。
