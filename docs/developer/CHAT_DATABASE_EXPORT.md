# 微信聊天数据库结构与导出指南

> 基于当前 Windows 微信数据库快照验证。本文只保留查询和开发需要的核心结构。

## 1. 核心结论

微信聊天数据不是放在一张总消息表里，主要分为四层：

1. `contact.db`：把微信号、备注、昵称解析为内部 `username`。
2. `message_0.db`：按会话拆成多个 `Msg_<MD5>` 消息表。
3. `session.db`：会话列表、摘要、未读、草稿，不是完整消息正文。
4. `message_resource.db`、`media_0.db`、`hardlink.db`：图片、文件、语音和本地媒体索引。

查询某个联系人时，正确链路是：

```text
微信号 / alias / username
  -> contact.db.contact.username
  -> Msg_ + MD5(username UTF-8)
  -> message_0.db.Msg_<md5>
  -> real_sender_id -> message_0.db.Name2Id.rowid
```

群聊最可靠的判断条件是：

```text
username 以 @chatroom 结尾
```

## 2. 数据库分工

| 数据库 | 关键表 | 用途 |
|---|---|---|
| `contact/contact.db` | `contact`、`name2id`、`chat_room`、`chatroom_member` | 联系人、群、群成员 |
| `session/session.db` | `SessionTable`、`SessionUnreadListTable_1`、`Name2Id` | 会话摘要、未读、草稿 |
| `message/message_0.db` | `Name2Id`、`Msg_<md5>` | 普通聊天正文主库 |
| `message/biz_message_0.db` | `Name2Id`、`Msg_<md5>` | 公众号和业务消息 |
| `message/message_resource.db` | `MessageResourceInfo`、`MessageResourceDetail` | 图片、文件等资源与消息的关联 |
| `message/media_0.db` | `VoiceInfo`、`Name2Id` | 语音数据和索引 |
| `message/message_fts.db` | `message_fts_v4_*`、`name2id` | 消息全文搜索派生索引 |
| `general/general.db` | `revokemessage`、`transferTable`、`redEnvelopeTable` | 撤回、转账、红包等状态 |
| `hardlink/hardlink.db` | `*_hardlink_info_v4`、`dir2id` | 本地图片、视频、文件索引 |
| `favorite/favorite.db` | `fav_db_item`、`Name2Id` | 与联系人或会话相关的收藏 |
| `head_image/head_image.db` | `head_image` | 头像缓存 |
| `solitaire/solitaire.db` | `Solitaire_<md5>` 等 | 群接龙数据 |

`sns.db`、`emoticon.db` 等不属于普通聊天主链，导出工具默认不处理。

## 3. 主要表结构

### 3.1 联系人和群

`contact.db.contact` 的关键字段：

| 字段 | 含义 |
|---|---|
| `id` | 联系人在本库中的数字 ID |
| `username` | 跨库使用的内部唯一标识 |
| `alias` | 用户设置的微信号，通常是工具输入值 |
| `remark` | 本地备注 |
| `nick_name` | 昵称或群名 |
| `is_in_chat_room` | 是否属于已保存群成员 |
| `chat_room_type` | 群相关类型字段 |

群表关系：

```text
chat_room.id
  -> chatroom_member.room_id

chatroom_member.member_id
  -> contact.id
```

本版本中还验证了：

```text
contact.id = contact.db.name2id.rowid
```

### 3.2 消息分表

`message_0.db.Name2Id`：

```sql
CREATE TABLE Name2Id(
    user_name TEXT PRIMARY KEY,
    is_session INTEGER
);
```

当 `is_session != 0` 时，该 `user_name` 对应一个聊天分表：

```text
table_name = "Msg_" + MD5(user_name UTF-8).hexdigest()
```

当前快照中：

- 普通消息分表：149 张；消息 182,411 条。
- 其中群会话 33 个，非群会话 116 个。
- 业务消息分表：137 张；消息 5,563 条。

`Msg_<md5>` 的核心字段：

| 字段 | 含义 |
|---|---|
| `local_id` | 本地消息 ID |
| `server_id` | 服务端消息 ID |
| `local_type` | 消息类型；低 32 位是基础类型 |
| `sort_seq` | 消息排序序列，优先用于时间线排序 |
| `real_sender_id` | 真实发送者 ID |
| `create_time` | Unix 秒时间戳 |
| `message_content` | 文本、XML 或其他消息正文 |
| `source` | `msgsource` 等附加信息 |
| `packed_info_data` | 未解析扩展结构 |
| `WCDB_CT_message_content` | 值为 4 时正文是 Zstd BLOB |
| `WCDB_CT_source` | 值为 4 时 `source` 是 Zstd BLOB |

真实发送者关系：

```text
Msg_<md5>.real_sender_id
  -> message_0.db.Name2Id.rowid
  -> Name2Id.user_name
```

不要解析正文开头的 `wxid_...:\n` 来判断发送者；`real_sender_id` 才是结构化关联键。

### 3.3 会话表

`session.db.SessionTable` 以 `username` 为主键，保存：

- 未读数和会话状态；
- 最后一条消息摘要；
- 最后发送者和消息类型；
- 会话排序时间；
- 草稿和隐藏状态。

未读表关联：

```text
SessionUnreadListTable_1.username_id
  -> session.db.Name2Id.rowid
```

`SessionTable.summary` 只是摘要，不能替代 `message_0.db`。

### 3.4 资源和媒体

资源库关系：

```text
MessageResourceInfo.chat_id
  -> ChatName2Id.rowid

MessageResourceInfo.sender_id
  -> SenderName2Id.rowid

MessageResourceDetail.message_id
  -> MessageResourceInfo.message_id
```

语音关系：

```text
VoiceInfo.chat_name_id
  -> media_0.db.Name2Id.rowid
```

本地媒体关系：

```text
image/video/file_hardlink_info_v4.dir2
  -> hardlink.db.dir2id.rowid
```

### 3.5 其他关联

```text
favorite.fav_db_item.fromusr_id
  -> favorite.db.Name2Id.rowid

favorite.fav_db_item.realchatname_id
  -> favorite.db.Name2Id.rowid
```

`general.db` 中以下表直接保存会话 `username`：

- `revokemessage.to_user_name`
- `revokebatchmessage.session_name`
- `transferTable.session_name`
- `redEnvelopeTable.session_name`
- `groupPayTable.session_name`

## 4. 跨库关联原则

每个数据库的 `rowid` 都属于自己的 ID 空间，不能跨库直接连接。

错误示例：

```text
message_0.Name2Id.rowid = contact.id
```

正确方式：

```text
message_0.Name2Id.rowid
  -> message_0.Name2Id.user_name
  -> contact.username
```

跨数据库永远先还原为 `username`，再连接其他库。

## 5. C++ 导出工具

源码：`src/native/weixin_chat_exporter.cpp`

构建：

```powershell
cmake --preset msvc-x64-vcpkg
cmake --build --preset release-vcpkg
```

依赖由 `vcpkg.json` 声明；JSON 使用 `nlohmann-json`，十六进制和 Base64
使用 Windows Crypt32。

运行：

在仓库根目录最简单的用法是只传微信号：

```powershell
.\build\wcdb-reader-vcpkg\Release\weixin_chat_exporter.exe '<微信号>'
```

它默认读取 `artifacts/exports/full_db_snapshot_20260731_final`，并在
`artifacts/exports/chat_export_<输入MD5>_<时间>` 创建结果目录。

需要指定其他快照或输出位置时使用完整参数：

```powershell
.\build\wcdb-reader-vcpkg\Release\weixin_chat_exporter.exe `
  --snapshot .\artifacts\exports\full_db_snapshot_20260731_final `
  --key-record .\artifacts\exports\wcdb_cipher_key_message_0.json `
  --wechat-id '<微信号、内部 username、精确备注或昵称>' `
  --output C:\output\contact_export
```

解析优先级：

1. `contact.username`
2. `contact.alias`，即通常所说的微信号
3. 精确 `remark`
4. 精确 `nick_name`

同一优先级匹配多个联系人时工具会停止，要求改用内部 `username`。

### 导出内容

输出目录包含 `manifest.json` 和约 40 个 JSONL 文件，主要包括：

- 联系人、群信息、群成员；
- 普通消息、业务消息、真实发送者映射；
- 会话摘要、未读、草稿、删除记录；
- 消息资源、资源明细、语音；
- FTS 派生内容和范围；
- 撤回、转账、红包、群收款；
- 图片、视频、文件硬链索引；
- 头像、收藏、群接龙。

所有数据库均以只读模式打开。BLOB 使用 Base64 保存；消息正文和 `source` 的 WCDB Zstd 数据会自动解压为 UTF-8 字符串。

`message_fts.db` 的虚拟表需要微信私有 `MMFtsTokenizer`，因此工具读取标准 SQLite 的 `message_fts_v4_*_content` 影子表，不依赖微信进程。

## 6. 已验证结果

完整快照：

- 路径：`artifacts/exports/full_db_snapshot_20260731_final`
- `db_storage` 文件：93 个
- 总大小：254,369,824 字节
- 93 个文件逐一进行 SHA-256 对比，源和副本完全一致

真实单聊测试：

- 消息 28 条，与原 `Msg_<md5>` 行数一致；
- 28 条压缩 `source`、8 条压缩正文成功解压；
- 40 个 JSONL 文件逐行解析成功；
- manifest 行数与实际文件行数一致。

真实群聊测试：

- 消息 346 条；
- 群成员 136 条；
- 资源索引 158 条；
- 资源明细 447 条；
- 全部 JSONL 可解析，消息数与原分表一致。

测试过程没有修改微信原数据库，也没有在本文记录联系人标识或聊天正文。

## 7. 使用注意

- 必须在微信关闭后复制 `.db`、`.db-wal`、`.db-shm`，避免漏掉 WAL 中的最新事务。
- 导出目录必须为空，工具拒绝覆盖现有结果。
- 输出可能包含聊天正文、头像、语音和其他敏感 BLOB，应继续放在 `.gitignore` 覆盖的目录中。
- 微信升级后应重新核对表名和列结构。
- `message_fts.db` 是派生索引，完整聊天记录仍以 `message_0.db` 为准。
