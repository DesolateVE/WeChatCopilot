# Weixin 数据库与逆向工具链

这是从旧 `WeChatAIPlugin` 工作区迁入的独立研究套件，保留原有 CMake
布局，供 WCDB 原生读取、离线导出、调试器捕获和 IDA 分析使用。源目录继续保留作为备份。

```text
src/native/          C++ WCDB 原生读取器与聊天导出器
tools/offline/       SQLCipher/WCDB 离线校验和导出
tools/debugger/      x64dbg 捕获与消息向量监控
tools/ida/           FLAIR/FLIRT 与 IDAPython 辅助
scripts/             WCDB 构建和 compilation database 脚本
requirements/        Python 依赖
artifacts/ida/       WCDB 2.1.16 pattern、签名和符号清单
```

## 构建

第三方依赖和构建输出没有从旧项目复制。进入本目录后重新生成：

```powershell
Set-Location .\research\weixin-db
.\scripts\build_wcdb_static.ps1
cmake --preset msvc-x64-vcpkg
cmake --build --preset release-vcpkg
```

脚本默认使用本目录下的 `.deps/`、`.tools/`、`build/` 和 `artifacts/`。数据库、密钥、
日志和聊天导出必须保存在 `artifacts/exports/` 或仓库的 `local-data/`，两者都不应提交。

详细结论与用法从 [开发者文档](../../docs/developer/README.md) 开始；给 AI 的精简接力信息
位于 [逆向上下文](../../docs/ai/REVERSE_ENGINEERING_CONTEXT.md)。

