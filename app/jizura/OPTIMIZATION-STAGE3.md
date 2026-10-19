# 第三轮：排版准备、纹理句柄与临时画布

保留原版全部 860 个注册项、27 套风格和绘图顺序。在第二轮基础上减少重复排版和无用画布分配，原版 165 个文件不变。第二轮由固定夹具重建，核对引擎 SHA-256 后参与比较。

## 实现

- `port/canvas-hot.js` 复用每行字符数组和排版因子。缓存最多 128 项、4,096 个 Unicode 码点，超过 256 字的单行不驻留。尺寸、字距、字体度量和最终坐标仍逐次按原公式计算，保留浮点运算顺序。整列开关进入缓存键；替换原排版／小假名函数时走原计算路径。公共 `typesetLine` 返回值不进入此内部缓存。
- 复用静态扫描线和四张颗粒图的 `CanvasPattern`，按绘图上下文和源图尺寸隔离。没有复用整帧、歌词位图或动态滤镜结果。
- 辉光实际使用四分之一尺寸的 `small` 画布，因此不再仅因辉光启用而放大全屏 `scratch`。需要 `scratch` 的画面效果仍按注册表申请。

没有新增整屏缓存。背景预合成曾产生像素差异，未纳入最终构建；保留原版底色、渐变、纸纹的绘制顺序。失败与隔离记录保留在工作区 `output/jizura-stage3-evidence/`，最终结果单独保存。

## 验收

- 4,408 帧与原版逐像素一致，覆盖全部注册项、风格、字形、透明／分层模式、尺寸和颜色变化，以及裁剪和继承阴影状态。
- 576 帧与第二轮逐像素一致；六组各 96 个样本，包含完整 Canvas 像素读回。无警告或异常。
- 三组 PNG ZIP 实际生成并解码比对：普通 5 帧、透明 5 帧、前后景分层 10 帧，均与原版逐像素一致。
- 实际生成 640×360 H.264 High 短片，141,639 B；本次是无音轨编码检查，未重新验收音频。
- 232 个排版／纹理用例、1,099 个前轮热点用例、265 个字形分解用例通过。

在 1024×576、仅辉光且无需全屏复制的场景中，`scratch` 像素载荷由 **2,359,296 B 降为 16 B**，省去约 **2.25 MiB**。画面逐像素一致。此数值仅指这一张临时画布的载荷，不是整机或进程峰值；切片、缩放等效果仍需要其完整尺寸。

## 性能边界

同源主机 QuickJS 2024-02-14、BIGNUM 关闭、ASan／UBSan、16 MiB JS 限额下，合成排版负载的中位单次耗时 **0.495 → 0.287 ms（下降 42.0%）**。每组 2,000 次，共三组；包含横竖排、整列开关、不同字号和混合文字，所有坐标的校验值一致。使用固定数字字体度量，不包含 Canvas 或 P4 硬件。

| 桌面完整帧＋读回中位数，640×360 | 第二轮 | 第三轮 |
| --- | ---: | ---: |
| 黑白 | 9.8 ms | 10.2 ms |
| 纸张 | 11.8 ms | 11.7 ms |
| 红色 | 11.2 ms | 11.8 ms |
| 两万事件合成场景 | 9.7 ms | 10.0 ms |
| 仅背景：黑白 | 6.6 ms | 6.4 ms |
| 仅背景：纸张 | 8.7 ms | 8.6 ms |

桌面整帧与 P95 仍有升降，尚无跨场景一致提速。主要确定收益是解释器排版开销和特定场景的临时画布载荷；不能换算成板端 FPS。

最终引擎字节码 **2,038,212 B**，通过 8 MiB JS 限额、64 KiB 主机栈保护的加载／注册／解析／默认规划探针。64 位主机 JS 分配快照为 **4,751,623 B**，不包含原生画布或整机堆。本轮未烧录，完整 Canvas2D 后端和实板帧率仍待验收。

结构化结果、构建哈希和全部 P95 见 [OPTIMIZATION-STAGE3-RESULTS.json](OPTIMIZATION-STAGE3-RESULTS.json)。

## 复现

从仓库根目录运行：

```powershell
node app/jizura/tests/canvas-hot.cjs
node app/jizura/tests/frame-hot.cjs
node app/jizura/tests/glyph-kernel.cjs
python tools/jizura_optimized.py build --output ../output/jizura-optimized-stage3
python tools/jizura_optimized.py serve --output ../output/jizura-optimized-stage3 --port 8770
```

打开 <http://127.0.0.1:8770/optimization.html> 运行像素和整帧比较，打开 <http://127.0.0.1:8770/canvas-export.html> 检查输出和临时画布。主机排版测量使用已有同源 QuickJS 运行器：

```bash
python3 tools/test_jizura_canvas_quickjs.py \
  --runner ../output/jizura-optimization-evidence/quickjs-kernel \
  --build ../output/jizura-optimized-stage3 \
  --output ../output/jizura-stage3-evidence
```
