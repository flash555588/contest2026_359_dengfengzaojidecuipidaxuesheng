# 小雅中文 tiny 蒸馏实验

这是自行训练的研究学生模型流程，不是 SanoTTS 官方中文 INT8 发布包。
当前已验证的板载中文和英语固件保留，训练工具不访问板子。

## 固定输入与结构

- 教师：官方 `rhasspy/piper-voices` 的 `zh_CN-xiao_ya-medium`，
  ONNX、配置和 MODEL_CARD 均由 `teacher.lock.json` 固定提交及 SHA-256。
- 训练实现：`upstream.lock.json` 固定 SanoTTS 的源码和文本集。
- 小模型：时长 hidden=32；声学 hidden=48、token depth=3、frame depth=3；
  piperlite decoder channels=96,48,24,12。目标约 50 万参数，实际以导出统计为准。
- 量化先使用上游的每输出通道 INT8 权重方案。部分激活保留 12-bit 数值范围、
  用 int16 容器承载；这不是把所有计算强制变成 INT8。
- 教师 MODEL_CARD 将源数据 BZNSYP 标为非商业用途；保留该限制及来源说明，
  不把实验产物直接当作可自由商用的声音包。

## Linux / WSL 运行

单独创建环境，CUDA 12.8 的 PyTorch 构建用于 RTX 5060 / Blackwell。
CUDA 组件首次下载较大，后续使用 pip 缓存，不改已有硬件测试环境。
磁盘临时目录必须容得下数 GB 的 CUDA 依赖；不要使用容量较小的 `/tmp`。
CPU 也可运行：安装官方 CPU PyTorch 构建，并给下面的命令添加 `--device cpu`。

```sh
python3 -m venv /path/to/zh-distill/venv
mkdir -p /path/to/zh-distill/tmp
export TMPDIR=/path/to/zh-distill/tmp
/path/to/zh-distill/venv/bin/python -m pip install torch==2.10.0 --index-url https://download.pytorch.org/whl/cu128
/path/to/zh-distill/venv/bin/python -m pip install -r app/espdl-quickapp/speech/zh-distill/requirements.txt
python3 tools/prepare_zh_tts.py --output /path/to/zh-distill/prepared
/path/to/zh-distill/venv/bin/python tools/run_zh_tts.py \
  --prepared /path/to/zh-distill/prepared --output /path/to/zh-distill/probe --phase probe
```

先验证真实 CUDA 矩阵反向传播，再提取 32 条训练和 4 条保留验证的教师输出。
时长/声学学生各训练 200 步，导出浮点基准和量化前端，运行真实 C99 前端
与 PyTorch 的对照。C gate 使用上游阈值：latent correlation >= 0.99、
单 token 时长最多相差 1 帧、总帧数漂移不超过 1%。
探针不训练声码器，不能用它声称完整中文 TTS 音质或端侧速度已经达标。

然后运行完整三学生短流程：

```sh
/path/to/zh-distill/venv/bin/python tools/run_zh_tts.py \
  --prepared /path/to/zh-distill/prepared --output /path/to/zh-distill/smoke --phase smoke
```

`smoke` 使用 256 条训练文字及 140 条保留文字，200/200/100/100/100 步，
运行到声码器和声学模型联合微调。`full` 使用 628 条训练文字及相同保留集，
沿用上游 shipped 步数。每阶段保存命令、退出码、日志及成功产物的哈希回执。
`--dry-run` 只打印命令；失败不会伪造成功回执。

固定版本的公开训练入口缺少教师 Torch 转换和 decoder signature 导出脚本。
`tools/zh_tts_support/` 提供两个项目自有桥接实现，在 `prepared.json` 中另列哈希，
不冒充上游源码。转换教师先与 ONNX 对比 32 个保留输入、3 种长度，96 次全部
通过才允许用于蒸馏；signature 使用原始 ONNX 中间张量和上游同一池化函数。
当前流程到 s9 停止，避免调用公开仓库中其他尚未发布的质量反馈/打包脚本。

训练完成后，使用联合微调产生的 acoustic/decoder 检查点导出：

```sh
/path/to/zh-distill/venv/bin/python tools/run_zh_tts.py \
  --prepared /path/to/zh-distill/prepared --output /path/to/zh-distill/smoke \
  --phase quantize --device cpu
```

校准只读训练集；C 数值验收使用验证集第 0、17、63、127 行。前端检查时长漂移
与 latent 相关性，声码器要求与浮点参考相关性 > 0.99，两者均启用 ASan/UBSan。
它们验证量化实现，不能证明蒸馏模型已能清楚说话。检查点和成功阶段回执记录哈希，
检查点内容改变后不得沿用旧的量化导出回执。

## P4 诊断入口

`tools/prepare_zh_q8_probe.py` 将通过数值验收的四个 blob、运行时、来源与许可说明
固定为诊断夹具，使用“你好。”的中文前端 ID。`tools/test_zh_q8_host.py` 检查
原始/适配运行时 PCM 一致性、取消后重开与地址/未定义行为检查。
`tools/build_speech_runtime.py --zh-prepared <fixture>` 可将它编入独立测试入口：

```text
desktop speech zh-q8
desktop speech zh-q8-pie
desktop speech zh-q8-play
```

命令经持久 UI 任务启动语音工作线程。它不会把新声音替换到正式中文入口。
模型状态、arena、PIE 重排缓存都在堆上；总工作缓冲限制 8 MiB，大块无法容纳
重排缓存时回退标量。PIE 对 12-bit/int16 激活和 int8 权重作精确的 int16 点积，
末层仍保留 15-bit 激活；不为提速截断成 8-bit。每次 PIE 诊断先运行 672 个
真实硬件点积用例，再比较同一模型的标量/PIE PCM 哈希。
实板诊断应保留原固件，验证后恢复；未通过音质验收的模型不替换正式中文入口。

上游文本是有限的合成模板，适合验证流程，不代表广泛中文语料覆盖。
预处理去掉英文 `Variant N.` 装饰并按正文去重：原 2,176 行实际只有 768 个
独立正文，原验证句的正文全部也曾出现在原训练文件中，因此必须重新划分。
清洗后保留 12 条 smoke、128 条验证，剩余 628 条训练；不能宣称已有两千条
独立训练句。训练前检查正文无训练/验证交叉。`acceptance.jsonl` 是另外的中文验收句，
不用于训练或量化校准；覆盖多音字、变调、短句、数字、停顿和较长回答。

## 后续验收与 P4 适配

### 2026-10-17 听感反馈与声码器隔离修复

扩展语料后声学 hidden=96，完整模型实计 948,267 参数，四个 Q8 blob 共
968,448 字节。1,039 条训练句模型的独立 24 句 Whisper base 字错率约 26.8%；
补充 192 条短句后共 1,231 条，短句有所改善但整体字错率退步至约 76.9%。
保留两版，不把短句改善当作整体提升；识别指标不能代替听感。

用户听同句隔离对照反馈：教师 latent + 小声码器电音严重，小声学模型 +
官方声码器电音轻微。因此优先隔离训练声码器，保留声学和时长模型，不能把
主要问题归因于 INT8。浮点小声码器也存在问题；数值相关性通过不等于音质通过。
三句对照无削波，教师 latent 对齐的声码器输出存在 frame-hop comb excess
和教师弱音区域能量超量，作为训练假设，尚不能仅凭数值断言其为全部听感原因。

```sh
/path/to/venv/bin/python tools/repair_zh_tts_decoder.py \
  --prepared /path/to/prepared-short --trained /path/to/short-h96 \
  --output /path/to/decoder-tail-repair2000 --steps 2000 \
  --python /path/to/venv/bin/python
```

此实验从原声码器检查点续训，保持 piperlite 96/48/24/12 结构，使用训练集教师
latent，不更新声学/时长模型，不加入新的推理滤波。训练加入 frame-comb excess
和 quiet-ceiling 约束，关闭对抗项；原模型与新候选分别保存。回执记录输入/输出
哈希、命令、退出码，`quality_passed` 保持 false，须重新试听和验证量化后才可采用。

首轮 2,000 步已完成。三句教师 latent 对齐诊断的 frame-comb excess 分别由
2.153/1.989/0.994 降至 1.792/1.587/0.429，quiet-ceiling excess 也下降。
长句整体 RMS 基本一致（相对教师 0.7159 → 0.7134），但“你好”的 RMS 下降，
不能把所有杂音指标改善都理解为听感修复。新候选仍为浮点试听版，尚未获用户
听感确认，也未重新量化或部署；原模型保持不变。

用户随后确认两条修复试听仍有非常严重的电音，故首轮声码器修复明确失败，
不能采用或宣称电音减少 57%（该百分比仅为一条长句的诊断数值变化）。
下一候选改为 192/96/48/24 声码器，1,000,321 参数（原 314,977；完整教师
1,662,977），从已验证的官方教师转换权重按 importance 选择通道初始化，
不继承被拒绝的声码器检查点。6,000 步仅使用教师 latent，加入多周期波形
对抗/特征匹配和 log-mel 损失。这是更大模型的质量实验，尚不满足原始
1 MiB 总权重目标，不应沿用小声码器的 P4 延迟或内存测量。

```sh
/path/to/venv/bin/python tools/repair_zh_tts_decoder.py \
  --prepared /path/to/prepared-short --trained /path/to/short-h96 \
  --output /path/to/decoder-wide-perceptual6000 --steps 6000 \
  --python /path/to/venv/bin/python \
  --teacher-init-checkpoint /path/to/full-clean/voice/parity/piper-decoder-teacher.pt \
  --channels 192,96,48,24 --perceptual-losses --lr 0.0001
```

该 6,000 步候选已完成，仍需听感确认，不能仅根据 loss 下降判定电音已消除。
另行构建完整教师声码器（256/128/64/32）+ 1,039 句版本学生前端的对照，
总 Q8 blob 为 2,324,428 字节；四条保留句的声码器 C/浮点相关性均 > 0.99。
它是自行量化的官方教师权重，不是官方 INT8 发布包。整句工作区超过适配器
8 MiB 限额，适配器正确拒绝；仅允许直接在电脑上导出音质对照，不能宣称已在
P4 运行或沿用原 tiny 的资源结果。P4 支持此模型需后续分块和实板测量。

完整宽度教师的同流程 WAV 与官方 ONNX 原声 PCM16 最大只差 1 LSB；抽查
11 条训练 latent/audio 配对也在 PCM16 误差内。这些检查排除对应的转换和配对
错误，不替代对蒸馏模型的人工听感检查。

进一步反馈为“同一句中部分字有电音”；相同教师 latent 经完整教师 INT8
声码器生成的对照，用户确认“没有明显电音”。该对照的 C/浮点相关性为
0.999561；它只支持这一条对照的结论，不能推及所有中文和其他学生声码器。
在保留完整声码器的路线中，当前主要排查学生声学前端。

`tools/train_zh_front_frozen_decoder.py` 使用 1,039 条训练数据，从联合微调前
的声学检查点训练 3,000 步；完整教师声码器所有参数 `requires_grad=False`，
优化器只有声学参数，保存后逐张量验证教师权重完全不变。训练切片两端保留
16 帧 latent 上下文，仅中间 64 帧音频参与波形/频谱损失，避免把无上下文
切片边缘与完整教师音频直接比较。三处切片中心与完整解码最大差 < 2e-6。
这消除对应的训练目标边界差异，不表示已证明所有字尾电音的成因。

```sh
OMP_NUM_THREADS=4 MKL_NUM_THREADS=4 /path/to/venv/bin/python \
  tools/train_zh_front_frozen_decoder.py \
  --prepared /path/to/prepared-broad --trained /path/to/coverage-h96 \
  --decoder /path/to/teacher-decoder-standalone.pt \
  --output /path/to/front-frozen3000 --steps 3000
```

该轮已完成，实计声学可训练参数 598,854、声码器可训练参数 0，
完整模型 2,296,267 参数；仍需听感和量化验收。
该轮 24 条独立验收句全部成功导出；相同 Whisper base 自动识别字错率约
24.4%（先前 1,039 句版约 26.8%）。该数值只用于可懂度辅助检查，不能据此
认定字尾电音解决；“已完成”等短句仍不稳定，新声学检查点尚未量化。

随后用户指出“如果明”和“散步”仍有电音，“散步”听成 zhanbu。
原句实际输入音素为 `s an4 b u4`；替换为教师音素时长的对照仍错音并有电音，
说明该例不能只通过修改文本 G2P 或时长解决。训练包中 `s` 出现 253 次，
`sh` 1,851 次，`zh` 725 次。该不平衡仅作为改进假设，不据此断言全部成因。

新增 `--balanced-initials --acoustic <warm-start>` 实验，对 s/sh/z/zh/r/g/m/b
的中心切片均衡抽样，并分别抽取真实句首/句尾切片。句首只保留右上下文、
句尾只保留左上下文，避免固定双侧上下文导致两端原始音频永远不进入监督区。
从前一轮声学检查点训练 4,000 步，实计 16,000 个切片，其中句首 2,496、
句尾 2,324、以 s 为中心 1,032；教师声码器仍逐张量完全不变。
原始试听句用于开发诊断，不能再视作未经调参的盲测。另设 4 条新语境句检查
同音素迁移，不进入训练/量化校准。该候选尚未通过人工发音与电音验收。

1. 在独立中文句子上比较教师、浮点学生、量化学生的可懂度及听感；
   不能用训练 loss 代替音质验收。
2. 声码器用上游校准和 C/Python 对照；量化误差不达标时再进行 QAT，
   不通过降低阈值把失败包装为成功。
3. P4 适配 INT16 激活 × INT8 权重的卷积热点，检查整数溢出、对齐和尾部，
   先标量再 PIE，对照同一模型输出；中间缓冲在任务内动态分配。
4. 实板记录冷/暖首次 PCM、RTF、内存峰值、热点耗时、取消和其他业务响应。
   初始目标为暖启动 RTF <= 1、首次 PCM <= 1 秒，属于验收目标，不是已达成结果。
5. 只有音质、量化及实板验证均通过，才替换现有中文入口。
