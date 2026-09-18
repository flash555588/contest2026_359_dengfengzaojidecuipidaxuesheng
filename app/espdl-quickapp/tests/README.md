# v3 应用宿主测试

本目录保存 JS 回归、C 宿主用例与少量测试输入，不是实板测试结果目录。
返回[应用快照说明](../README.md)，已有输出见 [evidence](../evidence/README.md)。

## Node.js 回归

在仓库根目录、具备 `node --test` 的 Node.js 环境运行：

```bash
node --test app/espdl-quickapp/tests/dafeiyu.test.cjs app/espdl-quickapp/tests/package.test.cjs
node --test app/homeassistant/tests/homeassistant.test.cjs
```

打包测试会按字节检查 JS 与 C 资源。LF/CRLF 的变化可能导致资源一致性失败，特别是
Windows 自动转换换行符的检出；应核对实际源码字节和 Git 属性，不能删除断言掩盖差异。
根级 Home Assistant 的生成能力以[当前脚本说明](../../homeassistant/README.md)为准。

## C 与 QuickJS 用例

`run_security.py` 要求宿主 `cc`、AddressSanitizer/UndefinedBehaviorSanitizer 及匹配的
QuickJS 源码目录；`--quickjs` 指向源码而不是可执行程序。在满足依赖的 Linux/WSL 中，
可从仓库根目录运行：

```bash
python3 app/espdl-quickapp/tests/run_security.py --quickjs /path/to/quickjs-source
```

其他 `test_*.js` 和 C 用例可能需要额外参数或独立编译，应先阅读具体文件。
依赖缺失、用例跳过、构建失败和断言失败须分别记录；宿主通过不代表外设或真实服务已联通。
