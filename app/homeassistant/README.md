# Home Assistant：根级桥与内置资源

本目录保存 `qpk_homeassistant.c/.h`、内置 JS 的 C 资源、生成脚本和宿主回归。
它不是 Home Assistant 服务端。返回[应用索引](../README.md)。

## 两条后端必须区分

`qpk_homeassistant.c` 是旧的根级兼容桥：当前实现拒绝明文 HTTP，HTTPS 返回未支持，
并未提供可用 TLS 传输。不要把“要求 HTTPS”描述成“已经能通过 HTTPS 连接”。

当前 v3 原生集成使用 [overlay/apps/system/hass](../espdl-quickapp/overlay/apps/system/hass/README.md)
和桌面桥，支持受限局域网 HTTP。该路径令牌以明文发送，只能在可信网络使用。
原生入口、Portal 配置和验证边界见 [HASS.md](../espdl-quickapp/HASS.md)。

## 源码与派生文件

JS 源码在 [quickapp/homeassistant](../../quickapp/homeassistant/README.md)。保持
`app.js` 为 LF；桌面 overlay 的 JS 副本、两份 `homeassistant_resource.c` 与
`hass_ui_auth.h` 必须对应同一份已评审源码字节。

在仓库根目录执行：

```bash
python3 app/homeassistant/generate_homeassistant_resource.py
node --test app/homeassistant/tests/homeassistant.test.cjs
```

本主分支当前生成脚本只更新本目录的 C 资源，不会自动更新 overlay C 资源、JS 副本或
授权头。更新 UI 时必须单独核验这些输出，不能认为运行一次该脚本就已完成全量同步。
独立开发分支中的增强工具不代表已进入本目录。

现有宿主测试覆盖身份摘要、UI 边界与共享服务行为；不替代设备固件构建或网络实测。
服务地址和令牌通过设备配置，不写入源码、测试固件或公开日志。
