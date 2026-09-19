# Home Assistant 家居面板 Quick App

本目录保存家居面板的 [app.js](app.js)、[manifest.json](manifest.json)、浏览器预览
`preview.html` 和参考数据 `ref-db.json`。返回[快应用索引](../README.md)。

这不是 Home Assistant 官方客户端或小米官方米家客户端。米家等设备需要先接入用户
自己的 Home Assistant；本应用不负责小米账号登录或云端设备接入。

## 选择实际后端

当前 v3 overlay 的共享原生服务由 `system.homeAssistantService` 提供，面板与原生
LVGL 页面使用同一服务。该服务支持受限局域网 HTTP，不支持 HTTPS；令牌会明文发送，
仅应在可信网络中使用。配置、权限和设备入口见 [HASS.md](../../app/espdl-quickapp/HASS.md)。

根级旧桥 `app/homeassistant/qpk_homeassistant.c` 是另一条路径：它拒绝 HTTP，
HTTPS 也尚未实现。不能把这理解成“配置 HTTPS 即可连接”，或把两个后端的约束混用。
源码职责见 [app/homeassistant](../../app/homeassistant/README.md)。

## 编辑、资源与测试

保持本目录 JS 为 LF；修改后核验 overlay JS 副本、两份 C 资源和授权 SHA-256。
不能只复制新 JS 而继续使用旧授权头。当前生成脚本的输出范围见
[资源生成说明](../../app/homeassistant/README.md#源码与派生文件)。

从仓库根目录运行：

```bash
node --test app/homeassistant/tests/homeassistant.test.cjs
```

`preview.html` 用于浏览器侧界面观察，不能证明真实 API、令牌配置或设备端控制已通过。
不要把长期令牌、设备私有地址配置或 `/data` 内容写入此目录或公开日志。
