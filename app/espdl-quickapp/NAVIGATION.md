# 应用退出返回入口

快应用成功打开时记录当前入口：桌面快捷入口启动的应用退出后返回桌面，从「全部应用」启动的应用退出后返回应用列表。页面顶部返回按钮与边缘返回手势共用此规则；相机延迟启动也在页面提交时保留入口。应用替换继承原入口，打开失败不覆盖已有入口，返回桌面或打开普通面板会清理旧状态。

此前 `qapp_back()` 固定调用应用列表，导致从桌面进入智能家居、OuO 等应用后退出时突然出现列表。现在 `qapp_page_commit()` 记录来源，`qapp_back()` 按来源返回；`glass_apps()` 标记列表页面，`panel_hide()` / `panel_card()` 负责重置。

验证：构建和烧录见 `evidence/flash-navigation-62.log`。`diagnostics/check_app_return.py --origin home` 和 `--origin apps` 在实机执行智能家居、OuO、你好快应用的返回按钮/边缘返回，以及相机延迟启动后的退出检查；对应日志为 `evidence/navigation-62-home.json` 与 `evidence/navigation-62-apps.json`。
