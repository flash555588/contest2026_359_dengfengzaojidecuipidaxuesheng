# JIZURA 完整功能基线

先在桌面浏览器跑通完整原版，再处理 ESP 性能和移植。此目录包含全部上游功能，作为后续逐项移植和画面比对的依据。

上游：[852wa/JIZURA](https://github.com/852wa/JIZURA)，版本 **0.10.1**，提交 `fc16bfe43ea4a6c25a21f1caf04d17326de14f00`。2026-10-18 导入 165 个文件，约 32.5 MB。`upstream/` 从该提交直接归档导出，不含嵌套 Git 仓库，不改写原始代码或许可证。文件 SHA-256 见 [upstream.lock.json](upstream.lock.json)。

## 运行与重建

从本仓库根目录运行（仅需 Python 3）：

```powershell
python tools/jizura_full.py verify
python tools/jizura_full.py serve
```

打开 <http://127.0.0.1:8767/zh-hans/>。服务仅监听本机回环地址；浏览器内处理歌词、音频、项目保存与导出。字体与原版一样按需从 Google Fonts 读取，字体不可达时原版使用系统回退字体。需要原字形对比时应确认字体实际加载，不能把回退字体截图作为字形一致的证据。

在工作区 `output/jizura-full-build/` 重建七种语言、AE 脚本与 CEP 扩展：

```powershell
python tools/jizura_full.py build
python tools/jizura_full.py serve --directory ../output/jizura-full-build
```

`build` 保持导入源码不变，在单独目录生成产物，并核对七种浏览器语言版本与上游发布页面逐字节一致。`--output` 可以指定其他构建目录；`serve --port` 可以指定端口。

## 完整保留的功能

- 原版全部表达包与风格：布局、入场、保持、退场、装饰、文字处理、背景、运镜、画面效果与转场。
- 完整编辑器：简易／详细模式、原版字体系统、逐行与逐镜头设置、锁定、随机生成、历史、撤销、配色及风格选择。
- 歌词／音频：LRC、强调、镜头划分、间奏、音频解码、能量与节拍检测、轻点同步、时间轴及导出范围。
- 输出：项目 JSON、LRC、MP4（含音轨）、PNG 序列、透明 PNG、前后景分层 PNG、AE 数据。
- 七种语言页面、AE ScriptUI 源码／构建脚本与 CEP 源码／构建脚本／发行包。

## 验证

重建后打开 <http://127.0.0.1:8767/full-validation.html>，点「运行全部验证」。验证页加载未经改写的原版引擎，遍历表达注册项和风格、渲染镜头、分析合成节拍音频，并实际生成 MP4、三种 PNG ZIP、LRC、项目及 AE 数据。页面提供验证 JSON 与产物下载。测试素材是原创歌词和合成音频，不读取用户歌曲。

功能保留、构建成功与运行验收分别记录；浏览器编码器或字体不可用时，应保留具体结果。AE 脚本／CEP 能构建不代表已经在真实 After Effects 中验收。此目录的浏览器功能也不代表已经移植到 ESP32-P4。

2026-10-18 本地记录：165 个源文件哈希核对通过；七种语言页面重建与上游一致；860 个注册项、27 套风格累计渲染 4162 帧，未报告异常或警告。实际导出 H.264＋AAC MP4、普通／透明／分层 PNG ZIP、LRC、项目 JSON 及 AE 数据；12 秒合成节拍音频检出 120 BPM。字体实际加载包含 Noto Sans SC、Noto Serif SC、ZCOOL QingKe HuangYou。导出样本用于功能检查，不代表长视频、4K、全部字体、全部效果组合或 ESP 性能均已验收。完整记录及样本位于工作区 `output/jizura-full-validation/`。

早期 `espdl-quickapp` 中的精简 C/LVGL PV 保留为实验，不作为完整复刻完成的证据。后续以这里的原版功能、画面、时间关系与输出作为迁移对照，再处理 Canvas2D、字体、音频、内存、帧率及硬件后端。

ESP32-P4 的只读资源查询、同源 QuickJS 主机探针、内存预算和迁移边界见 [移植可行性评估](ESP32-P4-FEASIBILITY.md)。目前确认解析／规划可以复用，完整绘图、编辑器与导出尚未在板端验收。

原版之上的无损优化分支、可播放对比和测量说明见 [优化记录](OPTIMIZATION.md)。优化在独立构建目录应用，`upstream/` 仍通过全部 165 个文件哈希核对。

第二轮的批量精灵解码、事件遍历、渐变复用与整帧读回测量见 [第二轮记录](OPTIMIZATION-STAGE2.md)。第一轮引擎作为固定对照保留。

第三轮的有限排版缓存、纹理句柄复用、辉光临时画布与实际导出检查见 [第三轮记录](OPTIMIZATION-STAGE3.md)。第二轮也作为固定对照保留；当前默认优化构建包含第三轮。

27 套风格巡游、本地字体、原生 SD 配色入口及 `/sdcard/jizura` 素材包见 [SD 素材说明](SD-ASSETS.md)。

P4 默认仅保留聚焦、强调、逐字三套实时 LRC 效果，详见 [音乐说明](../espdl-quickapp/MUSIC.md)，新歌不需要生成歌曲帧素材。可选研究构建的完整画面预渲染到 SD、固定双缓冲播放路径与验证流程见 [设备播放说明](DEVICE-PLAYBACK.md)。它保留源项目画面，实时 Canvas2D、编辑器与板端导出仍是独立的后续移植工作。

## 许可证

保留 [原项目 MIT 许可证](upstream/LICENSE)（hakoniwa）与 [第三方声明](upstream/THIRD_PARTY_NOTICES.md)。Google Fonts 字体仍由各字体的许可证管理。歌词和音乐的权利归各自权利人。
