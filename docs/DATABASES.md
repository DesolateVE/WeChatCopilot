# 微信数据库指南

本文基于 Windows 微信 `4.1.12.26` 和仓库内当前 Schema 快照。表结构来自实际数据库，
字段含义则混合了样本验证、调用链证据与名称推测；微信升级后必须重新验证。

## 存储栈与只读原则

```text
微信业务存储
  -> WCDB
    -> SQLCipher（SQLite fork）
      -> SQLite pager / B-tree / WAL / FTS
        -> .db + .db-wal + .db-shm
```

WCDB 提供连接、ORM、迁移、压缩等能力；SQLCipher 负责页级加密。普通 SQLite 无法直接
打开原始文件，但正确配置 SQLCipher 后，内部仍是 SQLite 的表、索引和 WAL。

当前动态验证参数：

```text
WCDB key input       32 bytes
cipher page size     4096
cipher compatibility 4
journal mode         WAL
```

所有工具只应读取复制后的快照。采集快照时先退出微信，并复制同一时刻的 `.db`、
`.db-wal`、`.db-shm`；只复制主库可能漏掉尚未 checkpoint 的事务。

## 数据库地图

当前生成的 Schema 目录记录 18 个数据库、496 张表。日常聊天链主要涉及：

| 数据库 | 用途 |
|---|---|
| `contact/contact.db` | 联系人、群聊和群成员；身份解析入口 |
| `message/message_0.db` | 普通聊天消息，按会话分成 `Msg_*` 表 |
| `message/biz_message_0.db` | 公众号和业务消息 |
| `session/session.db` | 会话摘要、未读、草稿和删除状态 |
| `message/message_resource.db` | 图片、文件等资源与消息的映射 |
| `message/media_0.db` | 语音数据和会话映射 |
| `message/message_fts.db` | 消息全文搜索派生索引 |
| `hardlink/hardlink.db` | 本地图片、视频和文件路径索引 |
| `general/general.db` | 撤回、转账、红包等业务状态 |
| `favorite/favorite.db` | 收藏内容及名称映射 |
| `head_image/head_image.db` | 头像缓存 |
| `solitaire/solitaire.db` | 群接龙 |

`sns.db`、`emoticon.db` 等不属于普通聊天正文主链。完整表和列请搜索
[Schema 索引](ai/DATABASE_SCHEMA.md)，需要精确类型、默认值和建表 SQL 时读取
[database_schema.json](ai/database_schema.json)。

## 主查询链

```text
username / alias / remark / nick_name
  -> contact.db.contact.username
  -> message_0.db.Name2Id.user_name
  -> "Msg_" + MD5(username UTF-8)
  -> Msg_<md5>.real_sender_id
  -> 同库 Name2Id.rowid
  -> Name2Id.user_name
```

最重要的约束是：**数字 ID 和 `rowid` 只在所属数据库内有效。** 例如不能直接把
`contact.id` 与 `message_0.Name2Id.rowid` 连接。跨库关联必须先还原为字符串
`username` 或 `user_name`。

## 联系人与群聊

`contact.db.contact` 的常用身份字段：

| 字段 | 含义 |
|---|---|
| `username` | 微信内部唯一标识；跨库关联首选 |
| `alias` | 用户设置的微信号 |
| `remark` | 本地备注，可能重名 |
| `nick_name` | 昵称或群名，可能重名 |

群成员关系只在 `contact.db` 内连接：

```text
chat_room.id -> chatroom_member.room_id
chatroom_member.member_id -> contact.id
```

群聊通常以 `@chatroom` 结尾；工具还会查询 `chat_room.username` 补充确认。不要根据昵称、
人数或消息正文猜测会话类型。

## 消息分表与字段

`message_0.db.Name2Id` 保存 `user_name` 和本库 `rowid`。已验证的表名算法：

```text
table_name = "Msg_" + MD5(username UTF-8).hexdigest()
```

Schema 快照中的普通消息库与业务消息库分别有 152 和 138 张 `Msg_*` 表；这是当前快照
状态，不是协议常量。

`Msg_<md5>` 的核心字段：

| 字段 | 用途 |
|---|---|
| `local_id` / `server_id` | 本地/服务端消息 ID |
| `local_type` | 消息类型；低 32 位通常是基础类型 |
| `sort_seq` | 时间线排序首选字段 |
| `real_sender_id` | 发送者在同库 `Name2Id` 中的 `rowid` |
| `create_time` | Unix 秒时间戳 |
| `message_content` | 文本、XML 或其他正文 |
| `source` | `msgsource` 等附加信息 |
| `packed_info_data` | 尚未结构化的扩展数据 |
| `WCDB_CT_message_content` / `WCDB_CT_source` | WCDB 存储/压缩标记 |

真实发送者关系：

```text
Msg_*.real_sender_id -> 同一 message 数据库的 Name2Id.rowid -> user_name
```

不要依赖正文开头可能出现的 `wxid_...` 文本。时间线优先按 `sort_seq, local_id` 排序。
若 `local_type` 带高位标志，可拆为：

```text
base_type = local_type & 0xFFFFFFFF
flags     = local_type >> 32
```

## 压缩与 BLOB

当前样本确认：

```text
WCDB_CT_message_content = 4 -> message_content 为 Zstd BLOB
WCDB_CT_source = 4          -> source 为 Zstd BLOB
```

数据通常以 Zstd frame magic `28 B5 2F FD` 开始。SQLCipher 只负责页解密；WCDB 可按
列配置透明解压。`chat_exporter` 为消息表配置这两个压缩字段，`db_explorer` 则对所有
BLOB 做受限探测：成功时显示解压文本，失败时保留十六进制。

其他 BLOB（如 `packed_info_data`、`extra_buffer`）不能仅凭类型断言为 Zstd。

## 会话、资源与媒体关系

`SessionTable.summary` 只是最后一条消息摘要，不是完整正文。常用同库关系：

```text
session.SessionUnreadListTable_1.username_id -> session.Name2Id.rowid

message_resource.MessageResourceInfo.chat_id
  -> message_resource.ChatName2Id.rowid
message_resource.MessageResourceInfo.sender_id
  -> message_resource.SenderName2Id.rowid
message_resource.MessageResourceDetail.message_id
  -> message_resource.MessageResourceInfo.message_id

media.VoiceInfo.chat_name_id -> media.Name2Id.rowid
hardlink.*_hardlink_info_v4.dir2 -> hardlink.dir2id.rowid
favorite.fav_db_item.fromusr_id/realchatname_id -> favorite.Name2Id.rowid
```

消息与资源跨库核对应组合使用 `username`、本地/服务端消息 ID 和时间，不能假设单个
数字 ID 跨库稳定。

## FTS 限制

部分 FTS 虚拟表依赖微信私有 `MMFtsTokenizer`，离线查询可能报
`no such tokenizer`；这不表示数据库损坏。读取派生索引时优先使用标准 SQLite 可读的
`*_content` 等影子表，完整聊天正文仍以 `message_0.db` 为准。

## 密钥与 SQLCipher 派生

`chat_plugin` 捕获的是传给 WCDB/SQLCipher 的 32 字节输入，不是已经派生好的数据库页
密钥。当前版本的离线验证过程为：

```text
salt = database_file[0:16]
page_key = PBKDF2-HMAC-SHA512(
  password = captured_key,
  salt = salt,
  iterations = 256000,
  dklen = 32
)
```

若直接使用 WCDB API，应把捕获值作为 key input 并设置页大小 4096、兼容级别 4；
若绕过 KDF 使用 SQLCipher 原始 `x'...'` key，则必须传入派生后的 `page_key`。每个数据库
首页 salt 不同，因此共享输入也会得到不同页密钥。

## 更新结构文档

```powershell
$env:WECHAT_DB_KEY_HEX = '<64 位十六进制 WCDB key>'
$env:WECHAT_DB_DIR = (Resolve-Path '.\local-data\db-storage').Path
xmake run db_explorer -- --schema-json docs/ai/database_schema.json
python tools/generate_schema_doc.py
```

生成文件只能包含 Schema。若新版本出现表名、字段、压缩标记或关联变化，应同时更新
本文，并区分“已验证”“名称推测”和“未知”。
