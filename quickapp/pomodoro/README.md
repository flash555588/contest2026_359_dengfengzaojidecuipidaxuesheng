# 番茄钟 Quick App

包名、版本和入口以 [manifest.json](manifest.json) 为准。`engine.js` 保存 JS 状态逻辑，
`face.js` 与 `app.js` 提供界面组合和入口，`preview.html` 用于浏览器预览。
返回[快应用索引](../README.md)。

设备端依赖项目的 QuickJS/QPK 运行时，浏览器预览不包含完整设备环境。
原生 C/LVGL/QPK 适配及资源生成说明见 [app/pomodoro](../../app/pomodoro/README.md)。

修改源码后，从仓库根目录运行 `python3 app/pomodoro/generate_pomodoro_resource.py`，
审查生成资源，并在目标运行时验证计时与界面行为。不要直接改内置资源数组，也不要把
浏览器预览正常视为已经完成固件验收。
