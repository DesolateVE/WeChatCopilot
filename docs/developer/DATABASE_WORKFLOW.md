# Windows 微信数据库工作流程

## 1. 结论

Windows 微信 4.1.12.26 的数据库底层是 SQLite，但准确说法是：

```text
微信业务存储层
  -> WCDB 数据库框架
    -> SQLCipher（带页级加密的 SQLite fork）
      -> SQLite pager / B-tree / SQL / WAL / FTS
        -> .db + .db-wal + .db-shm
```

WCDB 不是另一种独立数据库引擎。它在 SQLite/SQLCipher 之上提供对象映射、
连接池、事务封装、迁移、全文检索、损坏修复和字段压缩等能力。腾讯官方
说明 WCDB 基于 SQLite 和 SQLCipher，并支持 Zstd 字段压缩：

- <https://github.com/Tencent/wcdb>
- <https://tencent.github.io/wcdb/>

SQLCipher 则是在 SQLite 基础上增加页级加密、HMAC 完整性校验和密钥派生：

- <https://github.com/sqlcipher/sqlcipher>
- <https://www.zetetic.net/sqlcipher/sqlcipher-api/>

因此普通 `sqlite3` 不能直接打开微信原始 `.db` 文件；使用正确密钥和参数
通过 SQLCipher 解密后，内部仍是标准的 SQLite schema、表、索引、查询和
WAL 语义。

## 2. 本机实证

本次动态和离线验证得到：

```text
WCDB SetCipherKey:
  key input length       = 32 bytes
  cipher page size       = 4096
  cipher compatibility   = 4

message_0.db:
  journal_mode           = wal
  table count            = 153
  Msg_* table count      = 148
  message row count      = 180606
  PRAGMA quick_check     = ok
```

离线工具显示 `SQLite 3.51.1 / SQLCipher 4.12.0 community`，这是本次读取
副本所用驱动的版本，不代表微信进程内嵌组件的精确小版本。能确定的是该库
使用 SQLCipher compatibility 4 的文件参数。

## 3. 账号启动与数据库初始化

当前观察到的启动顺序：

```text
登录并确定账号数据目录
  -> 打开 all_users/login/<wxid>/key_info.db
  -> 打开账号目录下的 contact.db
  -> 打开 message_0.db
  -> 按业务需要继续打开 session、FTS、资源、媒体等数据库
```

每个 WCDB `Database` 对象可以先构造、后首次查询。WCDB 官方也将这种
行为称为 lazy initialization：真正的 SQLite handle 通常在第一次操作时
才创建。

本次在 `RE_WCDB_Database_SetCipherKey` 入口观察到：

```text
RCX = WCDB Database/数据库包装对象
RDX = WCDB Data*，包含 32 字节二进制 key input
R8D = 4096
R9D = 4
```

断点与文件句柄的对应关系：

```text
hit 1  key_info.db
hit 2  contact.db
hit 3  contact.db 初始化后续阶段
hit 4  message_0.db
```

四次命中的 32 字节输入指纹相同，说明这些库共享同一个上层 key input。
但每个数据库文件首页都有独立 salt，所以最终 SQLCipher 页密钥不同。

`key_info.db` 最先打开这一点已经动态确认；它在业务上具体保存哪些密钥
或账号元数据，尚未做表级分析，不能仅凭文件名断言消息库密钥直接存放在
其中。

## 4. SQLCipher 页级加密流程

### 4.1 打开数据库

```text
32-byte WCDB key input
  + 当前 .db 文件前 16 字节 salt
  -> PBKDF2-HMAC-SHA512, 256000 iterations
  -> 32-byte AES page key
  -> SQLCipher 校验/解密 SQLite 页面
  -> SQLite 读取 schema、B-tree、索引和记录
```

本次验证使用：

```text
page_key = PBKDF2-HMAC-SHA512(
    password=captured_key_input,
    salt=database_file[0:16],
    iterations=256000,
    dklen=32,
)
```

捕获值是传给 `sqlite3_key()` 的二进制输入，不是最终页密钥。直接将捕获值
设置为 SQLCipher `x'...'` 原始 key 会绕过上述 KDF，并导致页 1 HMAC
校验失败。

### 4.2 读取

```text
业务查询/Storage API
  -> WCDB 生成或执行 SQL
  -> SQLite pager 从主库或 WAL 读取页面
  -> SQLCipher 校验 HMAC 并解密页面
  -> SQLite B-tree/索引返回列值
  -> WCDB 对 compression flag=4 的列做 Zstd 解压
  -> WCDB/微信构造 ChatMessage 等业务对象
```

### 4.3 写入

根据已定位的消息同步函数和数据库结构，可以得到以下流程：

```text
网络层收包、解密、反序列化
  -> ChatMessage 列表
  -> ChatMessageStorage / AddMessageListToDBForSync
  -> 选择会话对应的 Msg_<md5> 分表
  -> WCDB 对配置列做 Zstd 压缩
  -> SQLite 事务修改页面
  -> SQLCipher 加密并写入 WAL
  -> 后续 checkpoint 合并回主 .db
  -> 更新 session / FTS / resource 等旁路数据库
```

其中“写入消息主表”和 WCDB/SQLite/WAL 行为已有动态或文件结构证据；
多个旁路库更新的精确先后顺序仍需逐调用点追踪，因此不能假定它们属于
同一个跨库原子事务。

## 5. 主要数据库分工

本机账号目录中已确认：

| 数据库 | 主要表/用途 |
|---|---|
| `session/session.db` | 会话摘要、未读计数、草稿、删除记录 |
| `contact/contact.db` | 联系人、群、群成员、标签、票据 |
| `contact/contact_fts.db` | 联系人和群成员全文检索 |
| `message/message_0.db` | 普通聊天消息主体 |
| `message/biz_message_0.db` | 业务/公众号类消息 |
| `message/media_0.db` | `VoiceInfo` 等媒体索引 |
| `message/message_fts.db` | 消息与图片全文检索索引 |
| `message/message_resource.db` | 消息资源、发送者和会话资源映射 |
| `general/general.db` | 最近记录、撤回、红包、转账等通用业务状态 |
| `emoticon/emoticon.db` | 表情相关数据 |
| `favorite/favorite.db` | 收藏数据 |
| `sns/sns.db` | 朋友圈相关数据 |

这些数据库均使用 SQLite WAL 模式，运行时通常同时存在：

```text
name.db       主数据库
name.db-wal   尚未 checkpoint 的最新事务页面
name.db-shm   WAL 索引/共享内存
```

离线快照应复制同一时刻的三件套。只复制主 `.db` 可能漏掉 WAL 中的最新
消息。

## 6. message_0.db 的分表和映射

### 6.1 会话到表名

`Name2Id` schema：

```sql
CREATE TABLE Name2Id(
    user_name TEXT PRIMARY KEY,
    is_session INTEGER
);
```

消息表名算法已经对全部 148 张表验证：

```text
table_name = "Msg_" + MD5(user_name UTF-8).hexdigest()
```

结果：

```text
is_session != 0 的 Name2Id 行: 148
Msg_* 表:                     148
成功一一映射:                 148
未映射表:                     0
```

### 6.2 发送者映射

消息列 `real_sender_id` 对应 `Name2Id.rowid`：

```sql
SELECT user_name FROM Name2Id WHERE rowid = real_sender_id;
```

180,606 条消息的 `real_sender_id` 全部能映射到 `Name2Id.rowid`，没有
缺失发送者。

### 6.3 消息类型

部分 `local_type` 高 32 位包含附加标志，低 32 位才是基础消息类型：

```text
local_type_base  = local_type & 0xFFFFFFFF
local_type_flags = local_type >> 32
```

例如若原始值为 `0x3900000031`，基础类型仍为 `0x31`，即十进制 `49`。
离线导出同时保留原值、基础类型和高位标志，避免信息丢失。

## 7. WCDB Zstd 列压缩

本库统计：

```text
message_content 总行数:          180606
Zstd 压缩正文:                    48942
Zstd 压缩 source:                176552
```

压缩列以 SQLite BLOB 存储，开头为标准 Zstd frame magic：

```text
28 B5 2F FD
```

对应的 `WCDB_CT_message_content` 或 `WCDB_CT_source` 值为 `4`。微信内的
WCDB 会透明解压；普通 SQLCipher 驱动只负责页解密，因此离线工具需要再用
Zstd 解压并按 UTF-8 解码。

## 8. 已实现的离线流程

```text
受控暂停微信
  -> 复制 message_0.db / WAL / SHM
  -> 在 WCDB SetCipherKey 入口捕获 32-byte key input
  -> 从副本首页读取每库独立 salt
  -> 派生 SQLCipher 4 page key
  -> SQLCipher 只读打开并合并读取 WAL 视图
  -> MD5 恢复会话到 Msg_* 表映射
  -> Name2Id.rowid 恢复发送者
  -> Zstd 解压 message_content/source
  -> 全局按 sort_seq 导出 JSONL
```

相关脚本：

```text
tools/debugger/extract_wcdb_cipher_key.py  捕获运行时 key input
tools/offline/verify_wcdb_offline.py       只读打开和 quick_check
tools/offline/export_weixin_messages.py    统一导出全部消息分表
src/native/weixin_wcdb_reader.cpp          WCDB 原生只读与透明解压验证
```

完整导出结果：

```text
文件: artifacts/exports/offline_message_0_messages.jsonl
行数: 180606
会话: 148
大小: 287867828 bytes
无效 JSON 行: 0
时间范围: 2026-01-19T16:26:26+08:00
       -> 2026-07-30T14:59:43+08:00
SHA-256: 86ca7071f03e9c3ce2a19f20228e9e52b0f04fc80d712483ff04a267b1c9e202
```

该 JSONL 和密钥文件均包含敏感数据，已通过 `.gitignore` 排除。

另有一条更贴近微信实现的原生路径：

```text
captured 32-byte key input
  -> WCDB::Database(readOnly=true)
  -> setCipherKey(..., 4096, Version4)
  -> setCompression(Msg_*.message_content/source)
  -> WINQ 自动页解密与 Zstd 透明解压
```

该路径已在同一快照上读出 148 张分表和 180606 条消息，并把压缩正文直接
返回为 Text。详情见 `docs/WCDB_NATIVE_READER.md`。
