# chip：芯片与驱动 overlay

[esp32p4](esp32p4/README.md)保存 ESP32-P4 架构及驱动适配，是本目录的主要入口。
返回[仓库首页](../README.md)。

## 集成方式

团队 manifest 将部分不存在于目标树的架构目录映射到 NuttX。对于已经存在的
`common/espressif` 等目录，开发流程通过 [apply_esp32p4_overlay.py](../tools/apply_esp32p4_overlay.py)
进行集成，而不是依靠同名目录自动生效。运行前必须检查该脚本的目标工作区和机器路径。

历史桌面/相机发布的补丁链位于 [tools/patches](../tools/patches/README.md)，不要把直接
覆盖开发树与固定基线的补丁应用混为一谈。板级差异放在 [board](../board/README.md)。

## 来源与验证

保留原始版权、SPDX 与上游出处；第三方固定点见 [THIRD_PARTY.md](../THIRD_PARTY.md)。
改变 DMA、缓存、时钟或显示/相机路径后，构建成功仍不足以证明板上正确性，必须补充
对应镜像的实板证据。不要用其他 revision 的历史日志替代当前硬件验收。
