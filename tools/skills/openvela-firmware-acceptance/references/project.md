# 构建与证据入口

路径相对项目根。在已准备好依赖的 Linux/WSL 中构建：

```sh
python3 tools/build_bilibili_v3.py \
  --baseline /path/to/prepared-baseline \
  --workspace /path/to/isolated-candidate \
  --toolchain /path/to/riscv-toolchain/bin \
  --archive /path/to/openh264-2.6.0.tar.gz \
  --output /path/to/output/candidate \
  --storage-layout preserve
```

这是命令形状，所有输入路径换成当前真实路径。当前 `--archive` 是 `tools/install_bilibili_live.py` 校验的 OpenH264 2.6.0 源码归档，摘要必须匹配该安装器的 `H264_SHA`；不是任意应用包或固件压缩包。workspace 必须在 baseline 之外，已有 workspace 的基线标记必须匹配。Windows 交给 WSL 的复杂命令可写为脚本文件，避免多层 PowerShell/Bash 引号改变参数。

- 需要 USB 摄像头才加 `--usb-camera`。
- `--psram-xip` 是显式试验选项，正常构建默认关闭；依赖和物理验收见 `app/espdl-quickapp/DISPLAY-FLASH.md`。
- `--flash-auto-suspend-jedec`、`--dsi-flash-reload` 是为明确拒绝旧命令而保留的参数，不能当成功能选项。
- 语音选项按当前 backend、已校验 prepared voice 和 SD 模型确定，不照搬其他候选的配置。

复用证据格式可参考：

- `docs/TECHNICAL_CANDIDATE_ACCEPTANCE_2026-10-19.md`：已通过、未通过和剩余补证分列。
- `docs/evidence/psram-xip-20261021/README.md`：源码/镜像身份、启动闭包、Flash 实际写擦、实体观察与有限结论。
- `docs/PAGE_LIFECYCLE_AUDIT_2026-10-20.md`：宿主替身和真实运行边界。

建议为本次候选保存一份机器可读清单，字段按实际任务选取：源码摘要、prepared baseline、构建命令/选项、ELF/bin 摘要、板卡信息、烧录范围、回退身份、每项测试的类型/结果/证据路径，以及最终设备状态。不要把凭据、完整配置、图片 Base64 或未脱敏的设备数据放入共享清单。

历史教训：2026-10-18/19 NetSurf 离屏原型曾受旧对象 PSRAM 预留影响；20 次离屏渲染通过后仍明确没有长期验证。2026-10-13 跳帧试验出现全屏黑屏，回退校验和用户确认屏幕恢复分别记录，未将实验规则视为稳定候选。
