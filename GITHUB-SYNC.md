# 本地工作区与 GitHub 同步

目标仓库分支：`streetartist/contest2026_359_dengfengzaojidecuipidaxuesheng`
的 `dev-ai-contest-2026`。

按用户要求，以本地源码集合为准替换分支文件树。仓库根目录直接对应本地的
`01-v3.2-pinned`、`02-v3plus`、`03-early-v3`、`04-v3-20260913`、
`05-logs-and-handoff`、`diagnostics` 和根目录文档；不保留远端旧目录或额外的
`v3/20260915` 包装层。旧版本源码继续保留在本地已有的历史分组中。

GitHub 不允许该公开 fork 上传新的 Git LFS 对象。超过 50 MiB 的 5 个源码归档
因此按每卷最多 48 MiB 保存到同名的 `.tar.gz.parts/` 目录，其余文件直接提交。
克隆后在仓库根目录执行以下命令，还原原路径和原始内容：

```sh
python diagnostics/restore_source_archives.py
```

编译生成的 ELF／MAP／BIN、对象文件、调试内存转储、采集的照片与录音、
临时发布工具和设备服务密钥不纳入源码同步。源码自带的测试图片与界面资源保留。
历史归档内容保持原样；各版本的用途和验证边界以对应 README 为准。

`GITHUB-SHA256SUMS` 校验此次提交的实际文件，包括归档分卷。
`ARCHIVE-PARTS.json` 记录完整归档和每个分卷的大小、SHA256；还原脚本逐卷检查，
并校验合并结果。已有文件只有校验一致才会跳过，不会覆盖不一致的文件。
只检查分卷而不还原可运行 `python diagnostics/restore_source_archives.py --verify-only`。
原来的 `SHA256SUMS` 是 2026-09-14 历史导出记录，不能代替新同步清单。

当前设备仍为相机第 11 版。ESP-Claw 语音输入源码处于开发阶段，宿主测试通过，
但镜像体积超限，未烧录、未完成实板录音或在线问答。
