# 原生轮廓接口

原有 `ui.polygon(points, color, opacity, stroke, gradient)` 和 `ui.polygonSet(handle, points, color, opacity, stroke, gradient)` 保持兼容。`points` 是 `[[x, y], ...]`，包含 3～128 个点，坐标范围为 −8192～8192。轮廓自动闭合；`stroke=0` 表示填充。

高频动画可使用 `ui.polygonBuffer(buffer, color, opacity, stroke, gradient)` 和 `ui.polygonSetBuffer(handle, buffer, color, opacity, stroke, gradient)`。传入完整 `ArrayBuffer`，按本机字节序存放交错的有符号 32 位 x/y 坐标；建议由精确长度的 `Int32Array` 构造：

```javascript
const points = new Int32Array([20, 20, 80, 20, 20, 80]);
const handle = ui.polygonBuffer(points.buffer, 0xabcdef, 255, 0);
points[0] = 24;
ui.polygonSetBuffer(handle, points.buffer, 0xabcdef, 255, 0);
```

函数同步复制坐标，不持有 JS 缓冲区。调用返回后可以继续复用或修改该数组；只有更新接口会更新图形。不能直接传入 `Int32Array` 对象，也不能把一个子数组的整个底层缓冲区当作该子数组传入。缓冲区长度必须为 24～1024 字节且是 8 的倍数；已分离的缓冲区会被拒绝。

颜色为 RGB 整数，默认 `0xffffff`；透明度默认 255，范围裁剪为 0～255；线宽默认 0，裁剪为 0～8。可选渐变格式沿用数组接口：

```text
[kind, stopCount, x1, y1, x2, y2, radius, offset0, color0, ...]
```

`kind=1` 为径向，`kind=2` 为线性；色标数 2～5，位置在 0～1 之间且非递减。坐标与半径必须有限，径向半径必须大于零。坐标缓冲区在其他参数转换前完成快照，后续参数的 getter 即使分离缓冲区也不会造成悬空读取；更新时保留控件代际校验，避免参数转换期间删除并复用编号后写入新控件。

旧固件没有缓冲区接口时，应用应检查两个方法是否存在，再使用原有数组接口。Grok Ball 已实现该兼容回退。此接口与普通轮廓共享绘制路径、刷新裁剪、校验和生命周期，不提供应用专属超时或额外后台线程。
