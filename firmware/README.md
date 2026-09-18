# firmware：历史固件与验收材料

本目录按用途保留特定镜像及其报告，不提供一个适配所有 revision 的“最新通用固件”。
返回[仓库首页](../README.md)。

## 选择镜像

[esp32p4-desktop-v1](esp32p4-desktop-v1/README.md)对应 2026-08-30 的 v1.0 桌面验收记录；
[esp32p4-desktop-v1.0-release](esp32p4-desktop-v1.0-release/README.md)保留另一组历史发布材料。
[esp32p4-desktop-v3.2-candidate](esp32p4-desktop-v3.2-candidate/README.md)明确标为构建候选，
不能当作已通过 v3.2 实机验收的正式版本。

[esp32p4-sc2336-camera-v1.0](esp32p4-sc2336-camera-v1.0/README.md)是 SC2336 取帧材料；
[esp32p4-camera-preview-v1.0](esp32p4-camera-preview-v1.0/README.md)是 RGB565 预览交付；
[esp32p4-nsh](esp32p4-nsh/README.md)保存早期 NSH 镜像及 revision 分类。

## 校验与烧录

历史 SC2336 目录的补丁、defconfig 和 resolved.config 按 LF 字节登记摘要。Windows
自动转换为 CRLF 时，即使 Git 未显示内容修改，工作区的逐字节哈希也可能不同。应先核对
Git 原始内容和检出换行符，不要直接改写 SHA256SUMS 以绕过失败；这也不是固件重新验收。

先核对同目录的 `README.md`、`BUILD-METADATA.txt`、`TEST_REPORT.md`、`SHA256SUMS`
等实际存在的材料。不能跨目录取一个摘要去验证另一个镜像。仓库根目录可运行
`python3 tools/check_package.py`，检查已登记的固件摘要。

历史 NSH 烧录脚本使用 `0x2000` 应用偏移并检查 `0x400000` 的数据区边界；其他布局必须
使用该版本专门的说明。禁止不核对芯片 revision 和分区布局就烧录，禁止把模型当固件写入。

当前 v3 应用源码和证据在 [app/espdl-quickapp](../app/espdl-quickapp/README.md)，并不意味着
这里所有历史镜像都含有这些新功能。不要上传携带私有配置的整机 Flash 转储。
