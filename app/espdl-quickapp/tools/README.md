# 应用资源工具

本目录的 [convert_dafeiyu_sprites.py](convert_dafeiyu_sprites.py)将已有的桌宠 PNG 素材
转换为带校验的 LVGL RGB565A8 包。返回[应用快照说明](../README.md)。

脚本需要 Python 3 和 Pillow，并要求 `--sprites` 指向素材目录、`--output` 指向输出文件。
输入必须具有合法来源，不能因为完成了格式转换就认为取得素材授权；来源说明见
[桌宠 README](../overlay/apps/system/desktop/dafeiyu/README.md)。

在仓库根目录、依赖与素材均已准备好后运行：

```bash
python3 app/espdl-quickapp/tools/convert_dafeiyu_sprites.py \
  --sprites /path/to/dafeiyu-pet/sprites \
  --output /path/to/output/dafeiyu.lvbin
```

输出应放在明确的交付或设备安装目录，避免把生成物误加进源码。该工具不是完整 v3
固件构建器；历史 diagnostics 工具的边界见[版本历史](../../../docs/HISTORY.md)。
