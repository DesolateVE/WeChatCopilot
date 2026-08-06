# WeChatAIPlugin 迁移记录

迁移日期：2026-08-06  
来源：旧 `WeChatAIPlugin` 工作区  
目标：`D:\DesolateVE\WeChatCopilot`

本次采用非破坏性迁移：来源目录完整保留，没有删除或移动其中的文件。

## 已迁入

- 6 份 Markdown/Word 分析资料；其中 AI 接力文档加入了新仓库路径映射。
- 2 个 C++ 原生工具、3 个离线 Python 工具、3 个调试器工具和 3 个 IDA 工具。
- CMake/vcpkg 配置、2 个构建脚本和 Python 依赖声明。
- WCDB 2.1.16 的 `.pat`、`.sig` 与符号清单，共 3 个 IDA 分析产物。

二进制参考文档和三个 IDA 产物均已与来源文件做 SHA-256 对比，结果一致；
14 个代码、脚本和依赖声明文件也已逐文件校验一致。

## 未迁入

- `.deps/`、`.tools/`、`build/`、`.vscode/`：体积大且可重新生成。
- 来源仓库的 `.git/`：不把旧项目历史嵌套进当前项目。
- `artifacts/exports/` 和 `artifacts/logs/`：包含真实聊天导出、密钥、数据库快照与调试日志，
  不属于可共享的开发资料或 AI 上下文。

当前项目原有数据库快照已从 `db_storage_copy/` 归档到 `local-data/db-storage/`，并由
根目录 `.gitignore` 排除。应用中的硬编码密钥已移除，运行时必须通过
`WECHAT_DB_KEY_HEX` 提供。

