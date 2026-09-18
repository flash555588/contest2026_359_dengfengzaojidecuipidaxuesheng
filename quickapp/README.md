# quickapp：QuickJS 快应用源码

本目录是快应用 JS、manifest 和浏览器预览的源码入口。它不是完整桌面运行时，也不是
标准浏览器 Web 应用的统一构建包。返回[仓库首页](../README.md)。

## 应用入口

[camera](camera/README.md)提供相机快应用资料；[homeassistant](homeassistant/README.md)
提供家居面板；[pomodoro](pomodoro/README.md)提供番茄钟；
[hello_quickapp](hello_quickapp/README.md)是参赛快应用示例。
OuO 单独位于 [ouo](../ouo/README.md)。

## 开发边界

设备端依赖桌面提供的 `ui`、`system` 等接口。`preview.html` 只能验证浏览器模拟中的
界面行为，不能替代设备 API、外设或网络联调。包名、版本和能力声明以各自 manifest 为准。

内置版本通常需要生成 C 资源，并可能另有 overlay 副本。同步规则见
[原生应用目录](../app/README.md)，不要只修改生成的 C 文件。Home Assistant 的身份和
授权摘要绑定到特定 JS 字节，必须保持 LF 与源码一致性。
