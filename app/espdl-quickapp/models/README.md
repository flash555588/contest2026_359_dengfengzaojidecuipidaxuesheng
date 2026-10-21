# ESP-DL 模型与清单

本目录保存 ESP-DL 模型文件、[manifest.json](manifest.json) 和
[ESP-DL-LICENSE](ESP-DL-LICENSE)。返回[应用快照说明](../README.md)。

## 以清单为准

manifest 记录来源提交、权重布局、文件长度、摘要、模型偏移和模型包摘要。
当前 records 包含 ImageNet 分类、MSR 与 MNP 人脸模型；目录中的其他模型或变体
不等于已加入该清单，也不应自动打包或烧录。

当前运行时仅从 SD 卡加载，按 manifest 的文件名和 `sd_directory` 组织，路径为 `/sdcard/espdl/`。Flash 不再保留模型区；历史 manifest 中的模型偏移仅供旧版归档使用，禁止按旧模型区地址烧录到新固件布局。
模型包和固件的构建/烧录脚本部分保存在历史源码集合，位置见
[历史说明](../../../docs/HISTORY.md)。

## 来源与验证

逐文件检查长度和 SHA-256，保留模型来源、许可证和修改记录。自带许可证不能代替对
后来加入文件的来源核查；总台账见 [THIRD_PARTY.md](../../../THIRD_PARTY.md)。
模型能够加载或单次推理成功，不等于准确率、帧率和长期稳定性已经达标。


## 自定义唤醒模型

`nihao_openvela.gkw` 由 `tools/train_custom_wake.py --room-augmentation --training-multiplier 10` 生成，训练样本为 24120 个合成语音增强变体。训练、校准和限制见 `nihao_openvela.training.json` 与 `nihao_openvela.expansion.json`，运行流程见 `../speech/README.zh-CN.md`。它通过独立的语音资源流程部署到 `/sdcard/speech/custom/`，不加入 ESP-DL 视觉模型清单。更新时必须同步 `glass_wake_model.h` 并重新编译固件，保留旧 SD 模型用于回滚。
