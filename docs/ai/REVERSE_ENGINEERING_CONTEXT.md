# 微信逆向与聊天数据库：上下文恢复手册

> 研究更新时间：2026-07-31；仓库迁移：2026-08-06。详细证据位于
> `docs/developer/REVERSE_ENGINEERING_HANDOFF.md`、`CHAT_DATABASE_EXPORT.md` 和
> `DATABASE_WORKFLOW.md`。旧项目的代码路径现以 `research/weixin-db/` 为根；历史密钥、
> 聊天导出和数据库快照未迁入 AI 上下文，当前私有数据统一位于 `local-data/`。

## 1. 当前结论

- 目标版本：Windows 微信 `4.1.12.26`，业务模块 `Weixin.dll`。
- 收发消息、历史查询、WCDB 密钥入口、消息分表、字段压缩和联系人完整导出均已找到可复现路径。
- 当前最可靠的历史数据方案是：微信关闭后复制全量数据库，再运行 C++ 只读导出器。
- C++ 导出器支持输入 `username`、微信号 `alias`、精确备注或精确昵称，能够区分单聊和群聊并导出约 40 类关联数据。
- 发送函数的高层链路和 Protobuf 边界已定位，但尚未确认可安全调用的完整 ABI、线程和对象生命周期；不能把现有地址直接当成稳定发送 API。
- 微信当前已关闭。以前记录的 PID、模块基址、x64dbg 端口和 Live VA 都已失效，下次必须重新查询。

## 2. 重启后必须先做的事

IDA 镜像基址固定按本 IDB 的 `0x180000000` 计算；进程中的模块基址每次启动都可能变化：

```text
RVA     = IDA_VA - 0x180000000
Live_VA = 当前 Weixin.dll 基址 + RVA
```

恢复调试时按以下顺序：

1. 确认新注册的 Weixin/x64dbg MCP 实例、PID 和端口，不复用旧端口。
2. 在 x64dbg 中重新取 `Weixin.dll` 基址。
3. 只用本文的 RVA 计算 Live VA。
4. 重新设置断点并先保持禁用，避免 LongLink 后台流量造成断点风暴。
5. 用一条带唯一文本和明确时间的测试消息关联各层。

## 3. 功能函数逆向地图

### 3.1 发送链路

```text
业务发送入口
  -> kernel::foundation::CoCgiSendRequest
  -> newsendmsg Task
  -> SendMsgRequestNew
  -> 通用 Protobuf Serialize
  -> LongLink 封包、加密、Socket
```

| IDA VA | RVA | 已知作用 |
|---|---:|---|
| `0x18388D040` | `0x388D040` | `CoCgiSendRequest` 的发送消息路径 |
| `0x183891930` | `0x3891930` | 创建并提交 `newsendmsg` Task；path `/cgi-bin/micromsg-bin/newsendmsg`，Task type `522` |
| `0x183891C70` | `0x3891C70` | `newsendmsg` Task 构造函数 |
| `0x18065C720` | `0x65C720` | 通用 Protobuf 序列化；`RCX=消息对象`，`RDX=输出对象` |
| `0x180107570` | `0x107570` | 解码 `SendMsgResponseNew` |
| `0x183891FB0` | `0x3891FB0` | 发送完成业务回调 |

Task 内部已知布局：

```text
Task + 0xF0   SendMsgRequestNew
Task + 0x120  SendMsgResponseNew

SendMsgRequestNew + 0x08  repeated MicroMsgRequestNew* 数组
                  + 0x10  数量

MicroMsgRequestNew + 0x08 ToUserName
                    + 0x10 Content
                    + 0x18 Type
                    + 0x1C CreateTime
                    + 0x20 MsgSource
                    + 0x28 ClientMsgId
```

相关 Protobuf 虚表 RVA：

| 类型 | RVA |
|---|---:|
| `SendMsgRequestNew` | `0x8CD5FC8` |
| `SendMsgResponseNew` | `0x8CD60E8` |

### 3.2 接收与同步链路

```text
LongLink 收包、解密、解压
  -> newsync Task 响应缓冲区
  -> NewSyncResponse ParseFromArray
  -> 内部 ChatMessage
  -> AddMessageListToDBForSync
  -> WCDB / SQLite / WAL
```

| IDA VA | RVA | 已知作用 |
|---|---:|---|
| `0x182D91FF0` | `0x2D91FF0` | 创建 `newsync` Task；path `/cgi-bin/micromsg-bin/newsync`，Task type `138` |
| `0x182D92330` | `0x2D92330` | `newsync` Task 构造函数 |
| `0x181042D10` | `0x1042D10` | 将响应 AutoBuffer 解析到 `Task+0x140` |
| `0x18065C650` | `0x65C650` | 通用 Protobuf 反序列化；`RCX=目标对象`、`RDX=明文字节`、`R8D=长度` |
| `0x183731D30` | `0x3731D30` | `AddMessageListToDBForSync`，已动态自然命中 |
| `0x181748E00` | `0x1748E00` | `SaveSendMessagesAtOnce` |
| `0x182F99DC0` | `0x2F99DC0` | `ChatMessageStorage` 初始化 |

Task 与虚表：

```text
Task + 0xF0   NewSyncRequest
Task + 0x140  NewSyncResponse

NewSyncRequest  vtable RVA = 0x88CFD28
NewSyncResponse vtable RVA = 0x88CFDA8
```

`sub_18065C650` 是观察接收业务 Protobuf 明文的首选断点。条件应使用“当前模块基址 + 虚表 RVA”，不能复制旧 Live VA。

### 3.3 LongLink 锚点

| IDA VA | RVA | 用途 |
|---|---:|---|
| `0x184A6F75E` | `0x4A6F75E` | 首选发送侧关联点 |
| `0x184A75DC0` | `0x4A75DC0` | 接收工作函数 |
| `0x184A76748` | `0x4A76748` | 接收日志锚点 |
| `0x184A7698F` | `0x4A7698F` | 接收日志锚点 |

Task type、reqid 和线上 LongLink cmdid 不是同一个概念。当前只确认 `newsendmsg=522`、`newsync=138` 是客户端 Task type，不能据此宣称已恢复线上 cmdid。

### 3.4 历史消息查询

| IDA VA | RVA | 接口 |
|---|---:|---|
| `0x183779BE0` | `0x3779BE0` | `CoGetInitialMessageList` |
| `0x18377D5F0` | `0x377D5F0` | `CoGetPagedMessageListWithSortInterval` |
| `0x1837815D0` | `0x37815D0` | `CoGetPagedMessageListWithAnchor` |
| `0x18379AEA0` | `0x379AEA0` | `CoGetSessionMessageListWithPageFromDB` |
| `0x18376F510` | `0x376F510` | `CoGetInitialMessageListInTimeInterval` |
| `0x183773D80` | `0x3773D80` | `CoGetPagedMessageListInTimeInterval` |

日期跳转链已动态验证。返回值是三指针布局的 `std::vector<ChatMessage>`，单个对象大小 `0x2E0`；一次初始日期查询观察到返回 40 条。若只需要历史聊天，离线数据库导出已经比进程内调用稳定，只有研究 UI 查询行为时才应继续走这些接口。

### 3.5 ChatMessage 已确认字段

字段来自 `RE_ChatMessage_ORM_Metadata`（IDA `0x180A63660`）和动态对象对照：

| 偏移 | 字段 | 说明 |
|---:|---|---|
| `+0x144` | `local_id` | `uint32` |
| `+0x148` | `server_id` | `uint64` |
| `+0x150` | `sort_seq` | `uint64` |
| `+0x158` | `local_type` | `uint32` |
| `+0x160` | `real_sender_id` | `uint32` |
| `+0x164` | `create_time` | Unix 秒 |
| `+0x168` | `status` | `uint32` |
| `+0x16C` | `upload_status` | `uint32` |
| `+0x170` | `download_status` | `uint32` |
| `+0x174` | `server_seq` | `uint32` |
| `+0x180` | `message_content` | MSVC `std::string` |
| `+0x1A0` | `compress_content` | MSVC `std::string` |
| `+0x1C0` | `source` | MSVC `std::string`，常见为 msgsource XML |
| `+0x1E0` | `packed_info_data` | 待细化 |

`+0x18`、`+0x38`、`+0x58` 是三个标识字符串，但发送者、会话、实际发送者的精确命名仍需用单聊与群聊各做一次成对动态验证。

## 4. 数据库地图与关联

主查询链：

```text
输入微信号/username/备注/昵称
  -> contact.db.contact
  -> contact.username
  -> "Msg_" + MD5(username 的 UTF-8 字节)
  -> message_0.db.Msg_<md5>
  -> real_sender_id
  -> 同一个 message_0.db 的 Name2Id.rowid
  -> Name2Id.user_name
```

群聊判断：`username` 以 `@chatroom` 结尾；工具还会用 `contact.db.chat_room` 补充确认。不要用昵称、人数或消息正文猜测。

| 数据库 | 关键表 | 关联用途 |
|---|---|---|
| `contact/contact.db` | `contact`、`chat_room`、`chatroom_member` | 解析身份；`chat_room.id -> chatroom_member.room_id -> contact.id` |
| `message/message_0.db` | `Name2Id`、`Msg_<md5>` | 普通消息；`real_sender_id -> 本库 Name2Id.rowid` |
| `message/biz_message_0.db` | `Name2Id`、`Msg_<md5>` | 公众号、业务消息 |
| `session/session.db` | `SessionTable`、未读、草稿、删除表、`Name2Id` | 会话状态和摘要，不是完整正文 |
| `message/message_resource.db` | `MessageResourceInfo`、`MessageResourceDetail`、名字映射表 | 图片、文件等资源；detail 通过 `message_id` 关联 info |
| `message/media_0.db` | `VoiceInfo`、`Name2Id` | `chat_name_id -> 本库 Name2Id.rowid` |
| `message/message_fts.db` | `message_fts_v4_*_content`、`name2id` | 全文搜索派生数据；私有 tokenizer 不可离线加载 |
| `general/general.db` | 撤回、转账、红包、群收款等表 | 多数表直接保存会话 username |
| `hardlink/hardlink.db` | `*_hardlink_info_v4`、`dir2id` | 本地图片、视频、文件索引 |
| `favorite/favorite.db` | `fav_db_item`、`Name2Id` | 收藏来源和真实会话 |
| `head_image/head_image.db` | `head_image` | 头像缓存 |
| `solitaire/solitaire.db` | `Solitaire_<md5>` 等 | 群接龙 |

最重要的关联约束：每个数据库的 `rowid` 只在该库内有效。跨库连接必须先转成字符串 `username`，不能把 `contact.id` 与 `message_0.Name2Id.rowid` 直接 JOIN。

消息主表核心字段是 `local_id`、`server_id`、`sort_seq`、`local_type`、`real_sender_id`、`create_time`、`message_content`、`source` 和 `packed_info_data`。时间线优先按 `sort_seq, local_id` 排序。

压缩规则：

```text
WCDB_CT_message_content = 4  -> message_content 是 Zstd BLOB
WCDB_CT_source = 4           -> source 是 Zstd BLOB
```

普通 SQLCipher 只做页解密，不会透明处理 WCDB 列压缩；当前 C++ 导出器会显式 Zstd 解压。FTS 虚拟表依赖微信私有 `MMFtsTokenizer`，离线读取其标准 SQLite `_content` 影子表。

## 5. 加密、快照与敏感数据

- 动态确认的 WCDB 输入密钥长度为 32 字节，页大小 `4096`，`cipher_compatibility=4`。
- 密钥入口：`RE_WCDB_Database_SetCipherKey`，IDA `0x1805DBF40`，RVA `0x5DBF40`；入口 `RDX` 指向 WCDB Data，`Data+0x08` 为字节指针，`Data+0x10` 为长度。
- 当前密钥记录：`artifacts/exports/wcdb_cipher_key_message_0.json`。它包含 `key_hex`，属于敏感文件，禁止提交或对外发送。
- 封存快照：`artifacts/exports/full_db_snapshot_20260731_final`，包含 93 个 `db_storage` 文件，共 254,369,824 字节，复制后逐文件 SHA-256 一致。
- 不要用封存快照做反复回归；测试使用 `full_db_snapshot_20260731`。
- 复制数据库必须在微信关闭后进行，并同时复制 `.db`、`.db-wal`、`.db-shm`。

## 6. C++ 工具当前状态

核心文件：

```text
src/native/weixin_chat_exporter.cpp   按联系人导出全部关联数据
src/native/weixin_wcdb_reader.cpp     WCDB 原生只读和 schema 验证
vcpkg.json                            nlohmann-json 依赖
CMakePresets.json                     vcpkg/MSVC 构建预设
```

构建：

```powershell
cmake --preset msvc-x64-vcpkg
cmake --build --preset release-vcpkg
```

运行：

```powershell
.\build\wcdb-reader-vcpkg\Release\weixin_chat_exporter.exe '<微信号>'
```

完整参数见 `WEIXIN_CHAT_DATABASE_GUIDE.md`。实现约束：

- JSON 读取、JSONL、manifest 和终端摘要均使用 vcpkg 的 `nlohmann-json`，不再手写找 key 或 JSON 转义。
- 十六进制和 Base64 使用 Windows Crypt32。
- SQL 使用参数绑定；数据库用 `SQLITE_OPEN_READONLY` 与 `PRAGMA query_only=ON`。
- 输出目录必须为空，工具拒绝覆盖已有数据。

2026-07-31 最新回归：

| 场景 | 消息 | 群成员 | 资源索引 | JSON 验证 |
|---|---:|---:|---:|---|
| 单聊 | 28 | 0 | 0 | 41 个输出文件全部可解析，manifest 行数一致 |
| 群聊 | 346 | 136 | 158 | 41 个输出文件全部可解析，manifest 行数一致 |

## 7. 已证实与待完成

已证实：

- 收发 Protobuf 明文边界、`newsendmsg`/`newsync` Task、消息持久化入口。
- 历史消息和日期区间查询返回 `std::vector<ChatMessage>`。
- WCDB 密钥入口、SQLCipher 4 参数、消息分表散列、发送者映射和 Zstd 压缩。
- 单聊、群聊的离线全关联导出可运行且结果数量与原表一致。

待完成：

- 发送业务函数的完整 ABI、对象所有权、线程/协程约束和错误回调。
- LongLink 包头、加密前后字段、taskid/cmdid 的最终映射。
- `ChatMessage +0x18/+0x38/+0x58` 三个字符串的精确命名。
- 消息 `local_type` 的完整类型表及 `packed_info_data` 的结构化解码。
- 不同微信版本升级后的 RVA、表结构和压缩配置兼容性。

下次对话若继续功能函数逆向，先恢复当前模块基址并选择上面的一个未完成项；若继续数据库工具，先运行单聊和群聊回归，再修改导出范围或消息解析。
