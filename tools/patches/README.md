# 历史发布补丁集合

本目录保存针对 NuttX/apps 的可审查补丁和 [SHA256SUMS](SHA256SUMS)。
返回[工具索引](../README.md)。

## 固定基线与应用

[apply_final_overlays.sh](../apply_final_overlays.sh)固定要求 NuttX
`2f1387d56eb04ad2599baca58a3fa2380cdaaedb` 与 apps
`88827afd368d4bbb4802b96ed44d9582f85b2f92`，并校验工作区、补丁摘要及应用状态。
必须以脚本中的实际列表、顺序和 strip 参数为准，不能把文件名排序后全部盲目执行。

该补丁链用于对应历史桌面/相机基线，不等于 [v3 应用 overlay](../../app/espdl-quickapp/overlay/README.md)
的完整发布流程。基线不匹配时应先停止并调查，不能通过强制忽略错误继续覆盖。

## 修改与来源

变更补丁时同步检查摘要、调用脚本及匹配的构建证据，保留第三方来源和 SPDX 声明。
来源台账见 [THIRD_PARTY.md](../../THIRD_PARTY.md)，公共上游映射待办见
[UPSTREAM_PLAN.md](../../UPSTREAM_PLAN.md)。补丁能应用不代表源码已公开发布或实板验收完成。
