# ESP-Claw 语音输入开发中（2026-09-15）

基于已烧录的相机第 11 版开发。用户选择兼容 OpenAI 的语音转写接口。
**本目录尚不是可烧录版本，也没有完成麦克风采音或在线语音问答实测。**

已实现源码：桌面“ESP-Claw 语音”入口，开始说话／说完了／取消／录音测试，
后台采集、16 kHz 单声道 16 位 WAV、multipart 转写上传、中文转写解析，
以及转写文字提交现有 ESP-Claw 单次聊天。关闭页面请求取消。
诊断录音只保存在 RAM 文件 `/tmp/espclaw-record.wav`；语音提问的音频在内存中处理。

板载 ES8311 使用现有 I2S0 两个 16 位时隙，采集代码提取左时隙作为单声道；
这一声道选择尚需实板样本验证。驱动修正配置返回码，并补上 I2S 连续采集启动通知。

## 验证与阻塞

- RISC-V 编译和链接完成，镜像包含 `claw_voice_*` 与桌面入口。
- ASan/UBSan：WAV、二进制 multipart、UTF-8、转写解析和 20,000 个异常输入通过。
- Python WAV 与 MIME 解析器独立核对采样信息和上传文件字节通过。
- 使用模拟采集／网络后端验证重复请求、缺失配置、失败阻断、三个阶段的取消通过。
- **当前本地镜像 4,283,424 字节**，从 `0x2000` 烧录会越过 `/data` 起点 `0x400000`。
  不得烧录。已经对 TLS 使用 `-Os`，仍需进一步优化，保留现有数据分区位置。
- 未配置真实转写／聊天服务、密钥和信任证书；未发送真实音频到外部服务。
- I2S DMA 超时、主时钟、声道、缓冲区归还和停止流程仍需检查及实板验证。
  当前取消是协作式的：底层阻塞时保留工作线程及缓冲区，界面显示正在取消，
  不能据宿主测试宣称硬件取消有确定时间上限。
- 尚未完成桌面新页面实板截图与布局验收。

## 配置约定（尚未进行真实服务验收）

`/data/espclaw/voice.json` 需要以下字段。只在本地填入实际密钥，文件不纳入 Git。

```json
{
  "transcription_url": "https://your-service.example/v1/audio/transcriptions",
  "transcription_model": "YOUR_TRANSCRIPTION_MODEL",
  "transcription_api_key": "YOUR_TRANSCRIPTION_KEY",
  "chat_base_url": "https://your-chat-service.example/v1",
  "chat_model": "YOUR_CHAT_MODEL",
  "chat_api_key": "YOUR_CHAT_KEY",
  "ca_file": "/data/espclaw/ca.pem"
}
```

系统时间必须正确，TLS 验证证书链及主机名；CA 文件在首次初始化后保持不变。
配置上传和系统校时尚未形成用户界面。接口格式参考
[OpenAI 官方语音转写文档](https://developers.openai.com/api/docs/guides/speech-to-text)：
`POST /v1/audio/transcriptions`、multipart 的 `file`／`model` 字段和 JSON `text` 响应。
具体服务和模型由用户配置，不默认调用某个服务。

开发命令：`espclaw voice record 5`（仅本地录音）、`espclaw voice start 8`
（录音→转写→提问）、`espclaw voice status`、`result`、`stop`、`cancel`。
这些命令属于未烧录的开发固件，当前板上的相机第 11 版没有这些入口。
