# ESPClaw 触屏聊天与 HTTPS

设备的“ESPClaw”页面支持输入、发送、取消、重试和新对话，保留最近六轮对话。AI 配置在“连接设备”网页保存，命令行和触屏通过同一个配置加载入口使用它。

## AI 制作快应用

稳定性、保存卡死、滚动测量与“放弃创意”的实现及验证记录见 [CHAT-STABILITY.md](CHAT-STABILITY.md)。

完整的 [AI 快应用开发指导](overlay/apps/system/espclaw/guide/QUICKAPP_GUIDE.md) 会自动加入触屏聊天和 CLI 请求的系统上下文，不受网页自定义提示词 1,024 字节字段的限制。文档包含 16 节：任务流程、设备环境、工具、包结构、参考项目、布局、API、触摸和动画、输入、存储、资源限制、硬件边界、检查与反馈、故障处理、完整计数器程序及交付规范。

AI 可使用 `qpk_list_examples`、`qpk_read_example`、`qpk_read_draft`、`qpk_write_draft` 四个工具。工具定义源文件是 [tools.json](overlay/apps/system/espclaw/guide/tools.json)。参考项目为 [你好快应用](overlay/apps/system/espclaw/qpk/hello/README.md)、[2048](overlay/apps/system/espclaw/qpk/game2048/README.md)、[OuO](overlay/apps/system/espclaw/qpk/ouo/README.md)，每个项目都有实际源码、manifest、来源、SHA-256 和验证边界。构建脚本校验它们与固定固件源码一致，读取时按需写入 `/data/qpk/.examples/<示例>/`，再从真实文件读取并校验；已有不同内容的文件不会被覆盖。

使用方式：

1. 在聊天中描述应用，例如“做一个大数字计数器，有加一、减一和清零，退出后保留”。AI 阅读相关示例，提交完整脚本。
2. 草稿通过真实 QuickJS 的 compile-only 检查后，点侧栏“预览草稿”。这会用现有运行时在设备上运行；预览有自己的存储身份。
3. 点左上角返回，继续描述修改；新草稿有递增版本号，需要重新预览。运行时错误会记入草稿状态，AI 可以读取后修正。
4. 满意后点“保存应用”，创建 `/data/qpk/ai.<slug>/manifest.json` 和 `app.js`，之后从“全部应用”启动，重启后保留。

草稿独立保存在 `/data/qpk/.draft/draft0.json` 和 `draft1.json`，使用完整 JSON、递增版本和 SHA-256 校验。只改写较旧/损坏的另一份，保留当前完整副本；重启选择最新有效记录，两份都损坏时保留原文件并报告错误。`base_revision` 阻止旧请求覆盖新草稿。保存操作只接受当前预览版本，已有不同内容的包不会被覆盖；需要保留改版时可让 AI 换一个 slug 保存副本。“放弃创意”停止当前生成并持久化空草稿状态，保留聊天记录和已安装应用。

每个请求最多 8 轮工具迭代。持续收到数据就继续处理，支持手动取消。为容纳完整脚本，单次输出预算最低设为 4,096 tokens，保存在设备上的服务配置保持原值。普通聊天仍按指导简短回答。源码在工具参数和草稿中传递，按实际长度分配内存；安装时以实际存储写入结果为准。

## 流式回复、思考进度和长文本

触屏与 CLI 对 OpenAI 兼容和 Anthropic 接口都发送 `stream: true`，复用现有 NuttX webclient / mbedTLS 连接。SSE 解析器按事件边界组装 JSON，不假定网络分片或 HTTP chunk 与 token、字符、事件对齐。工具参数可以分片，只有收到完整结束标记并验证完整参数后才交给工具执行。断流、仅思考就耗尽输出额度、工具参数被输出上限截断会报告具体错误，已经显示的文本会保留。

配置页的最大输出 Token 可填 1–384,000，实际请求至少 4,096。服务商仍需支持所填数值；上下文窗口与单次最大输出是不同限制。新的空白配置默认 16,384，不改动用户已有的模型、地址或密钥。

原来的“超时”改为“无数据等待”：有效值至少 300,000 毫秒（5 分钟），最多 3,600,000 毫秒。旧配置小于 5 分钟时，在请求中使用 5 分钟，不改写原文件。每次收到应用数据（包括 SSE 心跳）都会重新计时，因此持续输出可以超过这个时长。取消检查约每 100 毫秒进行一次；天气继续沿用自己的固定期限。

界面实时显示连接、思考、接收正文、读示例和编写草稿等状态，显示已接收字节数和最新思考片段。如果服务返回 `reasoning_tokens`，显示服务端统计；没有统计时明确显示思考字节数，不将字节估算成 token。统计可能直到最后才由服务提供；不推测或生成模型未公开的内部思考。

聊天记录变化不再重建全部消息。历史文本拆成较小的标签，流式更新只改动当前气泡，重复文字不重绘，滑动期间延后排版更新但继续接收网络数据。长回复的聊天气泡保留 8 KiB 片段，流式阶段始终展示最新内容；完成后可点“阅读完整回复”异步分页查看。超过 8 KiB 的完整正文保存到 `/data/files/espclaw/reply-*.txt`，也可以从设备文件网页下载。文件独占创建，不覆盖旧导出。导出文件不随最近六轮历史轮换删除，需要时从文件管理清理。

设备不是 1M 上下文的本地模型。它有 32 MiB RAM 与 4 MiB 数据分区：单个聊天正文/思考缓冲上限 2 MiB，完整文本保存取决于剩余存储空间。工具参数和传输缓冲按实际数据增长，分配失败会返回内存不足；这不代表服务端模型达到了 token 上限。上下文请求仍保留完整开发指导和有界最近历史。

生成器不增加新硬件驱动。当前示例目录只开放上述固定项目，不能任意读取 `.data`、服务配置或用户文件。语法通过、预览启动、用户验收和安装成功是不同状态，文档要求 AI 分别说明。

主机验证：`diagnostics/test_qpk_authoring.py` 使用固件中的 QuickJS 和 ASan/UBSan，覆盖源码检查、分页读取、校验失败、路径限制、取消、旧版本拒绝、双副本恢复、安装条件与重启读取；同时运行 Hello、2048、OuO 和文档计数器的业务测试。`diagnostics/test_chat_core.py` 验证两种真实后端收到完整文档和工具定义，并经过真实工具调用完成读取、生成、错误修正。这里的模型 HTTP 对端是确定性测试替身；实板和外部模型结果另行记录。

维护文档和示例后运行 `python diagnostics/prepare_qpk_guide.py`。完整固件构建也会自动生成嵌入资源；示例源码变动会触发固定哈希不匹配，必须重新验证后再更新固定值。

## 服务配置

网页、存储和 ESPClaw 核心统一使用正式后端名称：

| 服务类型 | 配置中的 backend | API 基础地址示例 |
| --- | --- | --- |
| OpenAI 兼容 | `openai_compatible` | `https://api.openai.com/v1` |
| Anthropic | `anthropic_compatible` | `https://api.anthropic.com/v1` |

基础地址不包含 `/chat/completions` 或 `/messages`；核心会追加对应请求路径。模型名称和 API Key 使用所选服务商提供的值。网页不会读回已保存的 API Key，留空保存会保留原密钥。旧短名称 `openai`、`anthropic` 已从运行路径和配置写入接口移除，不能再作为别名使用。

CLI 的 `espclaw ask "hi"` 与触屏聊天使用相同配置验证、正式核心后端和 HTTPS。`espclaw status` 只报告配置和清理状态，不能作为联网成功的证明。

## 共用传输

天气原先通过实板验证的 IPv4 连接、mbedTLS BIO、证书链与主机名验证、WANT_READ/WRITE 重试及 TLS 1.3 会话票据处理提取到 `glass_https.c`。天气与 ESPClaw 都调用此实现，ESPClaw 的端口文件只负责注册其公共 CA 信任库。

请求各自持有超时、取消标记和诊断状态。TLS 库及调用方使用一致的 pthread 线程配置，支持天气与聊天同时联网。HTTP 使用 1.1 和 `Accept-Encoding: identity`，拒绝重定向，限制请求和响应大小。Anthropic 的 `x-api-key` 由真实核心后端提供。

失败诊断包含连接阶段、错误码和证书校验标记，不包含请求头、API Key 或服务端错误正文。界面区分配置、认证、模型、限流、超时、TLS 和响应解析错误。

聊天历史同样使用两份带 sequence 的记录轮换，能从完整的暂存副本恢复。保存失败后下一次保存会重新检查存储，不会永久锁死；原有聊天数据和服务配置不用于快应用示例。

## 验证

- `diagnostics/test_chat_core.py`：真实 ESPClaw 核心、两种后端、上下文、回复解析、取消与销毁；HTTP 对端为确定性测试替身。
- `diagnostics/test_chat_stream.py`：任意字节分片、UTF-8、CRLF、思考/usage、OpenAI 工具参数与 Anthropic 签名思考、截断拒绝。
- `diagnostics/test_chat.py`：生成结束前的实时正文/思考统计，断流保留片段，长文本保存与分页逐字节还原、历史恢复。
- `diagnostics/test_shared_https.py`：真实配置存储、CLI、触屏桥接、核心、NuttX webclient 和同版 mbedTLS，连接本地 TLS 1.2/1.3 服务。验证会话票据、认证头、证书/主机名拒绝、超时、取消、HTTP 错误、响应边界、并发和 20 次重连，并运行 ASan/UBSan。
- `diagnostics/test_weather.py`：天气解析器与真实 NuttX HTTP 客户端回归。
- `diagnostics/test_portal.py` 和 `diagnostics/test_portal_browser.py`：正式后端配置写入/拒绝旧别名，以及手输令牌、二维码、刷新恢复、重复连接和文件安装。

对应报告位于 `evidence/`。上述本机测试不等于外部 AI 服务或实板通过；实板结果另行记录。

当前已有模型和 `/data` 的设备更新使用 `diagnostics/flash_espdl.py --execute --firmware-only`。
