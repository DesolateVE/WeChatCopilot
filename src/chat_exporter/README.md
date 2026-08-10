# chat_exporter

使用 WCDB 只读连接微信数据库，按 `username`、`alias`、`remark` 或 `nick_name`
解析联系人/群聊，并导出该会话的消息和关联数据。

`username` 与 `alias` 精确匹配优先且视为唯一。备注或昵称命中多项时，程序会打印
带编号的候选列表并等待选择；自动化运行可用 `--select-username` 指定候选。

```powershell
$env:WECHAT_DB_KEY_HEX = '<64 位十六进制 WCDB key>'
$env:WECHAT_DB_DIR = (Resolve-Path '.\local-data\db-storage').Path
xmake run chat_exporter -- '<查询值>'
```

完整选项：

```text
chat_exporter <query> [--db-dir <db-storage>] [--key-record <key.json>]
              [--output <empty-directory>] [--select-username <username>]
```

默认输出到 `local-data/exports/chat_export_<查询哈希>_<时间>/`。输出目录必须不存在或为空，
程序拒绝覆盖已有导出。密钥记录必须包含字符串字段 `key_hex`，且不会被写入输出。

## 源码结构

- `main.cpp`：进程入口与统一异常处理。
- `options.*`：参数模型、命令行解析和默认输出路径。
- `application.cpp`：导出流程编排，不负责底层数据库访问或文件序列化。
- `database.*`：WCDB 只读连接、绑定参数和结果行转换。
- `contact_resolver.*`：四字段联系人检索、唯一性判断和多候选选择。
- `export_writer.*`：JSONL 导出、消息字段增强和 manifest 写入。
- `utility.*`：密钥加载/擦除、UTF-8 路径、哈希和时间工具。
- `model.hpp`：模块间共享的数据模型。
