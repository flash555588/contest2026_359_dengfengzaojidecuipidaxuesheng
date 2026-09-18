# docs：文档与证据导航

返回[仓库首页](../README.md)。这里保存跨目录说明；模块细节留在模块自己的 README 中。

## 阅读入口

[HISTORY.md](HISTORY.md)解释参赛目录、历史源码集合及其取回边界。
[LVGL_INSTALL.md](LVGL_INSTALL.md)保留 LVGL 安装和集成说明；实际版本应与所用构建树核对。
[evidence](evidence/README.md)收录专题运行/构建记录，而不是通用验收保证。

硬件适配见 [board](../board/README.md)，v3 应用文档见
[app/espdl-quickapp](../app/espdl-quickapp/README.md)，Home Assistant 集成见
[HASS.md](../app/espdl-quickapp/HASS.md)。固件、工具和 AI 日志分别见
[firmware](../firmware/README.md)、[tools](../tools/README.md)、[logs](../logs/README.md)。

## 文档边界

记录测试时说明日期、提交、芯片 revision、配置与镜像摘要，并区分宿主测试、构建、
烧录和实板验证。历史报告不能自动作为修改后版本的证据。不要在文档中保存令牌、
私有地址配置或设备数据；许可证与公开发布待办见
[THIRD_PARTY.md](../THIRD_PARTY.md)和 [UPSTREAM_PLAN.md](../UPSTREAM_PLAN.md)。
