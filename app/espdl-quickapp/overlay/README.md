# v3 应用与系统 overlay

本目录保存 `apps/` 与 `nuttx/` 的选定文件，不是完整系统源码，也不是可直接执行 `make`
的独立工程。返回[应用快照说明](../README.md)。

## 结构与集成边界

`apps/system/desktop/` 包含桌面、QuickJS 桥、门户、相机、ESP-DL、音乐、桌宠及内置资源；
`apps/system/espclaw/` 包含聊天相关适配；共享家居服务见
[apps/system/hass](apps/system/hass/README.md)。`nuttx/` 保存本快照依赖的架构、配置和系统改动。

集成时必须使用应用文档规定的基线、工具链与 ABI，并检查外部工作区的未提交修改。
本快照的历史准备/构建工具有一部分只存在于历史源码集合，不应假定当前仓库能从零
自动重建整个 v3 固件。取回方式见[版本历史](../../../docs/HISTORY.md)。

不要覆盖第三方许可证、把另一版本的对象文件带入链接，或把本目录整树覆盖到任意上游版本。
根级 Home Assistant 源码和本目录的副本必须保持一致，具体边界见
[根级资源说明](../../homeassistant/README.md)。

## 验证

应用级检查见 [tests](../tests/README.md)，已有记录见 [evidence](../evidence/README.md)。
发布前应记录最终解析配置和镜像摘要；旧版本日志不自动覆盖新集成结果。
