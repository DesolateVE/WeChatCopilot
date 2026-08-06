# 微信数据库开发者指南

> 基于当前快照的实际结构总结，只讲值得关注的入口、关系和限制。字段语义包含样本验证与合理推测，不是微信官方规范。

## 总览

当前快照包含 18 个数据库、496 张表。日常开发主要关注以下数据链：

```text
联系人输入
  -> contact/contact.db: contact.username
  -> message/message_0.db: Name2Id.user_name
  -> Msg_<MD5(username UTF-8)>
  -> 消息正文、真实发送者、资源和媒体
```

最重要的原则：**数字 ID 和 rowid 只在所属数据库内有效。跨库关联必须先转换成 `username`/`user_name`。**

## 核心数据库

| 数据库 | 关注点 |
|---|---|
| `contact/contact.db` | 联系人、群聊、群成员；身份解析入口 |
| `message/message_0.db` | 普通聊天消息，按会话拆成 `Msg_*` 表 |
| `message/biz_message_0.db` | 公众号和业务消息，结构与普通消息分表相近 |
| `session/session.db` | 会话摘要、未读和草稿；不是完整正文 |
| `message/message_resource.db` | 图片、文件等资源与消息的关联 |
| `message/media_0.db` | 语音数据和会话映射 |
| `hardlink/hardlink.db` | 本地图片、视频、文件路径索引 |
| `general/general.db` | 撤回、转账、红包等业务状态 |
| `message/message_fts.db` | 全文检索派生数据；虚拟表可能依赖私有分词器 |

## 联系人与群

`contact` 是身份解析入口，优先使用：

1. `username`：跨库稳定标识。
2. `alias`：用户设置的微信号。
3. `remark`：本地备注。
4. `nick_name`：昵称或群名，可能重名。

群关系：

```text
chat_room.id -> chatroom_member.room_id
chatroom_member.member_id -> contact.id
```

群聊通常可通过 `username` 以 `@chatroom` 结尾识别。`chat_room.ext_buffer`、`contact.extra_buffer` 等 BLOB 的内部格式尚未确认。

## 消息分表

`message_0.db.Name2Id` 保存 `user_name` 与本库 rowid。观测到 `is_session != 0` 的会话通常对应：

```text
Msg_<MD5(username UTF-8)>
```

消息表关键字段：

| 字段 | 用途 |
|---|---|
| `local_id` / `server_id` | 本地/服务端消息 ID |
| `local_type` | 消息类型；低 32 位通常为基础类型 |
| `sort_seq` | 时间线排序首选字段 |
| `real_sender_id` | 发送者在同库 `Name2Id` 中的 rowid |
| `create_time` | Unix 秒时间戳 |
| `message_content` | 文本、XML 或其他正文 |
| `source` | `msgsource` 等附加信息 |
| `packed_info_data` | 未解析扩展 BLOB |
| `WCDB_CT_message_content` | 正文存储/压缩标记 |
| `WCDB_CT_source` | source 存储/压缩标记 |

真实发送者必须按同库映射：

```text
Msg_*.real_sender_id -> message_0.Name2Id.rowid -> Name2Id.user_name
```

不要依赖正文开头的 `wxid_...` 文本判断发送者。

## 压缩与 BLOB

当前样本中，`WCDB_CT_message_content == 4` 和 `WCDB_CT_source == 4` 表示对应值为 Zstd BLOB。浏览器实现会对所有 BLOB 尝试 Zstd：

- 解压成功：显示正文并使用琥珀色标记。
- 解压失败：保留内容并以空格分隔的大写十六进制显示，使用蓝色标记。
- 解压上限：512 MB。

通用 BLOB 探测是便利功能，不代表每个 BLOB 都应该是 Zstd。

## 会话、资源与媒体

`SessionTable.summary` 只是最后消息摘要；完整正文必须读取消息分表。

```text
SessionUnreadListTable_1.username_id -> session.Name2Id.rowid
MessageResourceInfo.chat_id -> message_resource.ChatName2Id.rowid
MessageResourceInfo.sender_id -> message_resource.SenderName2Id.rowid
MessageResourceDetail.message_id -> MessageResourceInfo.message_id
VoiceInfo.chat_name_id -> media_0.Name2Id.rowid
*_hardlink_info_v4.dir2 -> hardlink.dir2id.rowid
```

资源表与消息表常可通过本地消息 ID、服务端消息 ID、会话 username 和创建时间联合核对；不要假设单个数字 ID 可跨库直连。

## FTS 限制

部分 FTS 虚拟表需要微信私有 `MMFtsTokenizer`，直接查询会出现 `no such tokenizer`。这不是数据库损坏。需要读取派生索引时，优先检查标准 SQLite 可读的 `*_content` 等影子表；完整聊天正文仍以 `message_0.db` 为准。

## 只读接口

- `GET /api/databases`
- `GET /api/tables?database=<相对路径>`
- `GET /api/schema?database=<相对路径>`
- `GET /api/schema/:table?database=<相对路径>`
- `GET /api/tables/:table?database=<相对路径>&page=1&pageSize=20`

离线导出全部结构：

```powershell
wechat_copilot.exe --schema-json docs/ai/database_schema.json
```

完整机器结构见 `docs/ai/database_schema.json`；面向 AI 的紧凑结构索引见 `docs/ai/DATABASE_SCHEMA.md`。

## 版本风险

- 微信升级后表名、字段和压缩方式都可能变化。
- 名称推测不能替代样本验证或调用代码分析。
- 复制数据库时应同时处理 WAL/SHM，并尽量在微信关闭后获取一致快照。
