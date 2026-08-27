# ESP32-P4 OpenVela 大赛移植任务书

文档日期：2026-08-27；正式交付仓：`flash555588/contest2026_359_dengfengzaojidecuipidaxuesheng`；工作分支：`codex/esp32p4-lcd-bringup`；目标硬件：ESP32-P4 Function EV Board，芯片 revision v1.0 与 v3.2。

## 一、任务目标

基于 OpenVela 大赛指定上游和团队已经跑通的 ESP32-P4 NuttX/apps 仓库，形成可由大赛仓 manifest 获取、可重复编译、可按芯片版本选择、可烧录并带有实机证据的 OpenVela 发行包。最终交付必须包含源码固定点、双版本 defconfig、构建入口、固件及哈希、烧录说明、测试矩阵和 AI Coding 日志，不能依赖构建时覆盖源码的临时脚本。

## 二、当前固定基线

| 组件 | 固定版本 | 状态 |
| --- | --- | --- |
| 团队 NuttX | `cd61ccdd11498e22c058c3b2540828f88e23172e` | 已推送，含 v1 启动兼容、v1/v3.2 配置和 HAL 补丁 |
| 团队 apps | `93fb5ac72249ae766cbeea9f0e3d484bdd6807f7` | 已推送，LVGL 版本与 v9.2.1 固定点一致 |
| LVGL | `59a6b61c9580b65089010c5273f2fcdd6c4d2aae` | 官方 v9.2.1 提交，manifest 已固定 |
| ESP HAL | `8d0a898910084206721a0892ab093021bca1496a` | 构建时固定并应用仓内补丁 |
| mbedTLS | `582ff482038db6e4010dbf6f943d97b05ad06ea5` | HAL 子模块固定点 |
| vendor/espressif | `afe1b8c5ec67ff76eda48ee84d9dd116df2814ba` | manifest 已固定 |
| 工具链 | Espressif `riscv32-esp-elf` GCC `15.2.0_20251204` | v1/v3.2 均完成编译，v1 已实机通过 |

## 三、已完成工作

### 1. 源码与构建集成

大赛 manifest 已改为直接引用团队 NuttX/apps 完整仓库，不再通过 linkfile 或 Python 脚本覆盖 NuttX 已跟踪路径。LVGL 从官方仓固定到 v9.2.1 的实际提交。NuttX 增加 `desktop-v1`，原 `desktop` 明确为 v3.2；apps 的 LVGL Kconfig patch 版本改为 1，与所用 LVGL 源码一致。

已增加 `tools/build_esp32p4_desktop.sh`。脚本要求显式提供 GCC 15.2.0 工具链路径，支持 `v1.0` 和 `v3.2`，使用 NuttX `configure.sh -S`、输出 ELF/BIN/HEX、解析配置、构建元数据和 SHA256SUMS，不在构建过程中改写源码。

### 2. v1.0 启动问题修复

首次清洁候选镜像能够被 ROM 加载，但在 HAL `B3` 后循环触发 `CHIP_LP_WDT_RESET`，实体屏黑屏，已经明确标记为失败候选，不能发布。

根因修复已进入团队 NuttX：在 Simple Boot 早期关闭 timer-group 与 LP flashboot watchdog；对 `CONFIG_ESP32P4_SELECTS_REV_LESS_V3` 使用已经实机验证的 v1.0 bias、90 MHz CPLL 与 PSRAM 初始化顺序；避免 v3.x 通用内存和外设时钟清理破坏 v1.0 MSPI 状态。该路径由 Kconfig 隔离，不改变 v3.2 初始化流程。

### 3. v1.0 构建和实机验收

通过镜像位于 `firmware/esp32p4-desktop-v1.0-release`：

| 文件 | 大小 | SHA-256 |
| --- | ---: | --- |
| `nuttx.bin` | 2947548 | `FDD73E4CEB43BFC7F3CBA030A3A0CA3A0A99784B78A6BDD1121BA59A9D75F62B` |

本机识别芯片为 ESP32-P4 revision v1.0，固件写入 `0x2000` 后 esptool 写后校验通过。连续两次硬复位均进入 NSH；`desktop_main` 运行，`/dev/fb0`、GT911 `/dev/input0` 注册，32 MiB PSRAM 用户堆可用。证据位于 `logs/v1-startup-fix-smoke-20260827.log` 和 `logs/v1-startup-fix-reset-cycle2-20260827.log`。

### 4. v3.2 构建状态

`esp32p4-function-ev-board:desktop` 已固定 `CONFIG_ESP32P4_REV_MIN_301=y`，清洁构建通过，LVGL 版本警告为零。候选产物位于 `firmware/esp32p4-desktop-v3.2-candidate`。当前连接的板卡是 v1.0，不能用它替代 v3.2 实机验收，因此 v3.2 只能标记为“构建通过、待对应硬件”。

## 四、剩余工作和验收标准

| 优先级 | 工作包 | 具体动作 | 完成证据/验收标准 |
| --- | --- | --- | --- |
| P0 | 全新目录可复现构建 | 从空目录用大赛 manifest 执行 repo init/sync；分别运行构建脚本生成 v1.0、v3.2 | 无本机 overlay；两个构建均返回 0；记录 repo manifest、工具链哈希、BUILD-METADATA 和 SHA256SUMS |
| P0 | 仓库公开可读性 | 在未登录环境测试大赛仓、团队 NuttX/apps 及所有固定 SHA | 三个仓库和指定提交均可匿名 clone/fetch；否则评委无法复现，必须在截止前改为公开或把源码纳入可访问仓 |
| P0 | v1.0 人工显示/触摸验收 | 检查桌面不黑屏、颜色和方向正确；点击四角、拖动、释放；验证坐标范围 | 保存屏幕照片/视频或测试记录；四角不越界、不镜像，按下/移动/释放事件完整 |
| P0 | v1.0 稳定性 | 断电冷启动不少于 5 次；持续刷新 30 分钟；执行 PSRAM 分配/释放压力 | 5/5 进入桌面和 NSH；无 WDT/panic；framebuffer 持续刷新；PSRAM 无错误和明显泄漏 |
| P0 | v3.2 实机验收 | 在真正 revision v3.2 板上烧录 v3.2 候选，检查 UART/USB、LCD、GT911、PSRAM、桌面 | 芯片 ID 为 v3.2；连续 5 次启动；`desktop_main`、`fb0`、`input0` 和 PSRAM 全部通过；生成独立日志和固件哈希 |
| P1 | v3.2 USB 变体 | 验证 USB console 枚举、断开重连、长输出和复位恢复 | 无永久卡死；重连后可进入 NSH；压力日志可追溯。未通过时只发布 UART 版 |
| P1 | 发布包收敛 | 删除或隔离失败候选；为每个可发布版本提供 README、flash 脚本、TEST_REPORT、SHA256SUMS | 发布目录中不存在状态不明固件；每个 bin 都能反查源码、配置、工具链和实机日志 |
| P1 | 文档一致性 | 统一主 README、任务书、manifest、配置名称、烧录偏移、flash size 和芯片版本说明 | 文档与解析 `.config`、固件哈希完全一致；v1/v3 不混用 |
| P1 | AI Coding 日志闭环 | 使用 contest log collector 校验 `logs/flash555588` 清单，补入本次会话 | manifest 中每条会话文件存在且哈希匹配；日志随大赛仓提交，不包含密钥和隐私数据 |
| P1 | 大赛 gatekeeper | 检查 git 状态、文件大小、许可证、敏感信息、构建/烧录命令和链接有效性 | 自动检查全部通过；失败项有明确豁免依据；最终提交号写入提交说明 |

## 五、执行顺序

先完成匿名可读性检查和全新目录双版本构建，这是评委复现的前提。随后保持当前 v1.0 通过固件不被覆盖，完成屏幕/触摸人工测试与 5 次冷启动、30 分钟稳定性测试。拿到 v3.2 板后独立完成同样的硬件矩阵；没有 v3.2 实物时必须保留“未实机验证”标记。最后收敛发布目录、校验 AI 日志并运行 gatekeeper，再打最终 release/tag。

## 六、当前明确风险

团队 NuttX/apps 仓库若仍为私有，manifest 即使固定 SHA 也无法供评委匿名复现，这是当前最高发布风险。v1.0 已经进入系统，但实体屏幕显示质量和触摸四角仍需要操作者目视确认。v3.2 尚无对应实机证据，不能把编译成功写成移植完成。大赛仓仍存在历史未提交修改和诊断文件，本次提交必须按文件范围选择，避免把状态不明的二进制或回退代码混入正式发行。
