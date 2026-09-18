# 工具与 C6 宿主回归

本目录的 `test_*.py` 覆盖配置/overlay、C6 后端、RPC、链路状态、SDIO/CMD53 和 BLE 等逻辑。
返回[工具索引](../README.md)，集成说明见 [README.c6.md](../README.c6.md)。

## 运行前提

先检查目标测试的依赖。部分用例需要 C 编译器、外部源码或特定工具，不能仅因安装了
Python 就保证所有用例可运行。在满足用例前置条件的 Linux/WSL 环境，可从仓库根目录发现测试：

```bash
python3 -m unittest discover -s tools/tests -p 'test_*.py'
```

外部依赖缺失、跳过和真实失败应分别记录，不要把 skip 当作 pass。
Home Assistant 与桌宠回归另见 [应用测试](../../app/espdl-quickapp/tests/README.md)。
宿主测试不操作实际硬件时，不能据此声称已经完成无线、显示或摄像头验收。
