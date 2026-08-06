# AI 上下文入口

按任务选择上下文，不要一次加载全部文件：

- 逆向、函数地址、ChatMessage 布局、未完成项：读取
  [REVERSE_ENGINEERING_CONTEXT.md](REVERSE_ENGINEERING_CONTEXT.md)。
- 数据库关系与字段查询：先搜索 [DATABASE_SCHEMA.md](DATABASE_SCHEMA.md) 中的具体
  数据库、表或字段；需要精确 JSON 时再查询 [database_schema.json](database_schema.json)。
- 面向开发者的操作步骤和完整证据：转到 [../developer/README.md](../developer/README.md)。

固定约束：数据库只读；跨库不得直接连接数字 ID；推测不得表述为事实；不读取或输出
`local-data/` 中的密钥、聊天内容和账号标识，除非用户明确要求对本机样本做必要验证。

