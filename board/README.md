# board：板级支持

本目录放开发板初始化、板级 Kconfig、配置与引脚相关适配。芯片通用实现应放在
[chip](../chip/README.md)，应用页面应放在 [app](../app/README.md)。返回[首页](../README.md)。

## 目录边界

[esp32p4-function-ev-board](esp32p4-function-ev-board/README.md)是当前主要板级入口；
[esp32p4-common](esp32p4-common/README.md)提供公共板级头文件、链接脚本和支持代码。
[contest_board](contest_board/README.md)是参赛模板入口，不代表额外完成验收的硬件。

[esp32p4x_function_ev_board](esp32p4x_function_ev_board/README.md)保留早期硬件笔记，
开头已标明主要代码迁移。不要根据旧目录名推断实际板卡一定使用 v3.x 芯片。

## 使用前核对

以实际芯片 ID、板卡接线和对应配置选择 v1.x / v3.2 / USB 控制台方案，不要混用镜像。
桌面配置另见 [configs](../configs/README.md)，代码映射关系见
[团队 manifest](../contest2026_359_dengfengzaojidecuipidaxuesheng.xml)。

开发 overlay 和历史发布补丁链的基线不同，构建前阅读[根目录构建说明](../README.md#构建)。
烧录工具必须指定物理下载接口，不能仅凭固件控制台配置猜测下载方式。
