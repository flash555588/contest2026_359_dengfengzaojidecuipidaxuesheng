# Pomodoro：原生番茄钟适配

本目录保存番茄钟 C 状态逻辑、LVGL 页面、QPK 桥及内置资源。
返回[应用索引](../README.md)。

`pomodoro_engine.c/.h` 是 C 侧逻辑，`pomodoro_lvgl.c/.h` 负责 LVGL 适配，
`qpk_pomodoro.c/.h` 提供 QPK 桥。可编辑的 JS 界面和 manifest 位于
[quickapp/pomodoro](../../quickapp/pomodoro/README.md)。

## 更新内置资源

在仓库根目录运行：

```bash
python3 app/pomodoro/generate_pomodoro_resource.py
```

生成器按 `engine.js`、`face.js`、`app.js` 的顺序组合源码，写入
`pomodoro_resource.c`。不要直接修改生成的数组；生成后审查 diff，并在实际集成的
桌面运行时中验证。浏览器预览、源文件存在与固件已经接入是不同状态。
