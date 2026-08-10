# WCDB 聊天导出器

`src/chat_exporter/` 是当前项目内独立的 xmake 目标。它只通过 `WCDB::Database` 以
只读模式连接数据库，不直接调用 `sqlite3_open_v2` 或 `sqlite3_key`。

## 联系人解析规则

同一个输入值依次按以下优先级匹配 `contact.db.contact`：

1. `username`：唯一，命中后直接选择。
2. `alias`：唯一，命中后直接选择。
3. `remark`：可能重复。
4. `nick_name`：可能重复。

备注或昵称在相同优先级命中多个联系人时，交互终端会列出每项的 `username`、
`alias`、`remark` 和 `nick_name` 并要求输入序号。非交互调用使用
`--select-username <username>`；指定值必须属于本次候选集合。

如果联系人表没有该 `username`，工具还会检查 `message_0.db.Name2Id` 中的会话映射。

## 构建与运行

```powershell
xmake f -m release
xmake build chat_exporter

$env:WECHAT_DB_KEY_HEX = '<64 位十六进制 WCDB key>'
$env:WECHAT_DB_DIR = (Resolve-Path '.\local-data\db-storage').Path
xmake run chat_exporter -- '<username、alias、remark 或 nick_name>'
```

也可以读取本机密钥记录：

```powershell
.\build\windows\x64\release\chat_exporter.exe '<查询值>' `
  --db-dir .\local-data\db-storage `
  --key-record C:\private\wcdb_cipher_key.json `
  --output .\local-data\exports\selected-chat
```

完整参数：

```text
chat_exporter <query> [--db-dir <db-storage>] [--key-record <key.json>]
              [--output <empty-directory>] [--select-username <username>]
```

输出目录必须不存在或为空，防止覆盖旧结果。未指定时使用查询值的 MD5 和当前时间生成
目录名，不在目录名中泄露联系人信息。

## 输出范围

每类记录使用单独的 UTF-8 JSONL 文件，BLOB 使用带长度信息的 Base64 对象表示。
当前共生成 40 类 JSONL 加 `manifest.json`，包括：

- 联系人、群信息、群成员；
- 普通消息、业务消息和消息发送者；
- 会话、未读、未读统计、删除与草稿状态；
- 消息资源、资源明细、语音和资源 FTS 范围；
- 消息 FTS 四个分区及范围记录；
- 撤回、转账、红包、群收款、最近转发/搜索、好友消息和 VoIP；
- 图片、视频、文件硬链接及检查点；
- 头像、收藏和群接龙数据。

消息分表按 `Msg_<MD5(username UTF-8)>` 定位。WCDB 配置消息表的 Zstd 压缩字段后，
`message_content` 和 `source` 会以解压后的文本导出；时间线按 `sort_seq, local_id`
排序，群消息通过同库 `Name2Id` 补充 `real_sender_username`。

`manifest.json` 记录解析后的联系人、会话类型、消息表、文件名和每类行数，但不包含
数据库密钥。所有输出属于私有数据，只能保存在 `local-data/` 或其他明确的私有目录。
