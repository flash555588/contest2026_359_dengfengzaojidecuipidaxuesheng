# Design Notes

## State IDs

- `00-09`: offline, boot, idle, sleep, wake, listen, speak, charge, low power, update.
- `10-19`: happy, excited, shy, calm, sad, angry, surprised, curious, confused, proud.
- `30-41`: think, search, read, code, plan, wait, success, warning, error, retry, connect, sync.

ID 一旦发布不重新编号。未知 ID 回退到 `02`。

## Message Protocol

```json
{"emotionId":"30","tips":"正在分析任务"}
```

`handleAIMessage` 接受对象或 JSON 字符串。`tips` 可选，最大显示长度由 UI label 自然裁剪。

## Rendering

应用只调用当前 OpenVela runtime 已实现的 API：`background`、`panel`、`rect`、`text`、`button`、`setText`、`setPos`、`setSize`、`setColor`、`setOpacity`、`onSwipe`、`storage` 和 timer。没有任何外部视觉资产。
