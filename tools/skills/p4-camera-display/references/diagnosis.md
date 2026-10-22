# 故障决策与已有实验

所有路径相对项目根。

| 观察 | 优先检查 |
| --- | --- |
| 预览撕裂、分区颜色异常 | CSI/DSI 是否同时持有同一页，QBUF 与 scanout 的时序，缓存同步 |
| 只输出少数帧后等待 | 缓冲数量和回收条件；等待回调是否在被阻塞的工作队列中 |
| 对话/草稿保存时整屏闪烁 | 实际 Flash 写擦、显示 IRQ 间隔；不要只测相机 |
| 稳定低地址 MTVAL 且 LVGL 空指针栈可复现 | 共用路径中的空控件；Bot 页面可能没有 draft_label |
| P4 异步 bus fault | 同时留存 MEXSTATUS、STPC2/1/0、LDPC1/0，按 SDK 顺序读；MEPC 可能已前进 |

OpenOCD 的 MCAUSE 文字不能单独证明 PMP 错误，不能据此放宽保护或重写 trap。

主要入口：

- `app/espdl-quickapp/DISPLAY-FLASH.md`、`MULTIMODAL-CAMERA.md`。
- `app/espdl-quickapp/overlay/nuttx/arch/risc-v/src/esp32p4/camera_usb/`。
- `tools/protect_p4_display_flash.py`、`tools/instrument_p4_timing.py`。
- `tools/prepare_p4_psram_xip.py`、`tools/test_p4_psram_xip.py`。

历史证据只说明相应版本和测试窗口：

- 2026-09-03 对话：刚显示的 USERPTR 页立刻 QBUF 导致采集/扫描冲突；双回调等待和单缓冲候选均暴露失败。
- 2026-10-20 对照：禁用相机/USB仍闪，IRQ 最大间隔 66.107 ms；Bot 历史避开 Flash 后约 16.736 ms。该结果定位一条触发路径，未证明任意 Flash 写擦安全。
- cache-off IRQ 在通用返回链取指异常；自动挂起在 Bot 写入中 PSRAM store bus fault；DSI reload 仍有实体闪烁且性能下降，均已拒绝。构建器显式拒绝旧自动挂起/reload 参数。
- `docs/evidence/psram-xip-20261021/README.md`：显式 XIP 候选在本轮 3862 次写入、387 次擦除及两轮用户观察中通过；最长 IRQ 间隔 16.612 ms。长期老化、断电冷启动和动态 Flash mmap 并发不在该结果内，正常构建默认关闭。

按任务读取对应证据，先核对当前源码和构建选项。不得把历史物理地址、镜像摘要或板卡状态当作当前值。
