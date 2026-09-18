# v3 应用验证证据

本目录保存开发过程中形成的构建、烧录、启动、运行时和 UI 检查记录。
返回[应用快照说明](../README.md)。

## 如何阅读

先通过文件内的日期、命令、配置、镜像摘要和设备信息确定测试对象。
`firmware-validation.json`、相关 flash 日志和 runtime/UI JSON 属于不同阶段，
不能把其中一个文件的成功状态扩展成“所有功能都已通过”。

Home Assistant 证据入口见 [HASS.md](../HASS.md)，聊天、音乐和硬件 API 分别见
[CHAT.md](../CHAT.md)、[MUSIC.md](../MUSIC.md)、[HARDWARE.md](../HARDWARE.md)。
`pre-*` 等历史子目录用于解释修复前状态，不应被误当作发布产物。

## 添加证据

保存真实输出，记录对应源码与镜像，不编造测试结果。失败和未验证项也应保留。
公开前检查服务令牌、Wi-Fi 凭据、个人信息和设备数据；不要上传完整私有 Flash 转储。
这里的设备/测试日志不是 [AI Coding 会话](../../../logs/README.md)，不能代替参赛对话记录。
