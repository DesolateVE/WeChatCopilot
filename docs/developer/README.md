# 开发者文档

推荐阅读顺序：

1. [数据库开发者指南](DATABASE_GUIDE.md)：当前浏览器与数据库关系的快速入口。
2. [当前聊天导出器](CHAT_EXPORTER.md)：`src/chat_exporter` 的查询、选择和导出接口。
3. [数据库工作流程](DATABASE_WORKFLOW.md)：SQLCipher、WCDB、快照和离线读取流程。
4. [历史聊天数据库导出](CHAT_DATABASE_EXPORT.md)：迁入研究工具的联系人解析与全关联导出。
5. [WCDB 原生读取器](WCDB_NATIVE_READER.md)：原生库构建、只读验证和 FLIRT 签名。
6. [完整逆向接力文档](REVERSE_ENGINEERING_HANDOFF.md)：收发链、历史查询、ABI 与动态证据。

本次筛选范围和校验结果见 [迁移记录](MIGRATION_NOTES.md)。

第 3 至 6 项从 `WeChatAIPlugin` 原样迁入，记录的是 2026-07-31、Windows 微信
`4.1.12.26` 的研究状态。文中的 `src/native/`、`tools/`、`scripts/`、`build/` 和
`artifacts/` 路径均以 `research/weixin-db/` 为根。历史数据库快照、聊天导出和密钥
没有迁入；当前私有快照位于 `local-data/`，只允许按任务需要做本机只读验证。
