# Weixin.dll 逆向参考

本文保留后续研究仍需要的稳定证据，删除已失效的 PID、调试器端口、运行时地址、临时
断点和本机产物路径。

## 范围与可信度

```text
目标版本  Windows 微信 4.1.12.26
业务模块  Weixin.dll
IDA 基址  0x180000000
方法      IDA 静态分析 + x64dbg 动态验证 + 离线数据库核对
```

本文地址只对该版本有效。进程每次启动后都应重新取得模块基址：

```text
RVA     = IDA_VA - 0x180000000
Live_VA = 当前 Weixin.dll 基址 + RVA
```

“已确认”表示至少有静态语义与动态样本、数据库结果或两类独立证据互相印证；字段名推测
和未完成项不会提升为事实。

## 当前结论

- 发送与同步接收的 Protobuf 明文边界、消息持久化入口已经定位。
- 历史消息普通分页和日期区间查询都返回已解密的 `ChatMessage` 集合。
- WCDB key 输入边界、`Data` 布局、4096 页大小和 compatibility 4 已动态确认。
- `username -> Msg_<MD5> -> real_sender_id -> Name2Id` 和 Zstd 列压缩已离线验证。
- 全量历史读取优先使用关闭微信后的数据库快照和 `chat_exporter`；进程内接口更适合研究
  UI 行为与协议边界。
- 发送链虽然已定位，但完整 ABI、线程/协程、对象所有权和错误回调仍未确认，不能作为
  安全的发送 API 调用。

## 消息收发链

```text
发送：业务层
  -> CoCgiSendRequest
  -> newsendmsg Task
  -> SendMsgRequestNew
  -> Protobuf Serialize
  -> LongLink 封包/加密
  -> Socket

接收：Socket
  -> LongLink 收包/解密/解压
  -> newsync Task 响应缓冲区
  -> NewSyncResponse ParseFromArray
  -> ChatMessage
  -> AddMessageListToDBForSync
  -> WCDB / SQLite / WAL
```

### 发送函数

| IDA VA | RVA | 作用 |
|---|---:|---|
| `0x18388D040` | `0x388D040` | `CoCgiSendRequest` 的发送消息路径 |
| `0x183891930` | `0x3891930` | 创建并提交 `newsendmsg` Task；Task type `522` |
| `0x183891C70` | `0x3891C70` | `newsendmsg` Task 构造函数 |
| `0x18065C720` | `0x65C720` | 通用 Protobuf 序列化 |
| `0x180107570` | `0x107570` | 解码 `SendMsgResponseNew` |
| `0x183891FB0` | `0x3891FB0` | 发送完成业务回调 |

序列化入口：

```text
RCX = protobuf message object
RDX = output string/buffer object
```

入口时输出缓冲区可能还没有最终字节。观察结果应保存 `RDX` 并在函数返回后读取，或者
直接检查 `RCX` 的请求字段。

Task 与请求对象布局：

```text
Task + 0xF0   SendMsgRequestNew
Task + 0x120  SendMsgResponseNew

SendMsgRequestNew + 0x08  repeated MicroMsgRequestNew* array
                  + 0x10  count
                  + 0x18  capacity

MicroMsgRequestNew + 0x08  ToUserName
                    + 0x10  Content
                    + 0x18  Type
                    + 0x1C  CreateTime
                    + 0x20  MsgSource
                    + 0x28  ClientMsgId
                    + 0x2C  未确认的附加字段
```

虚表 RVA：

```text
SendMsgRequestNew  0x8CD5FC8
SendMsgResponseNew 0x8CD60E8
```

### 接收与同步函数

| IDA VA | RVA | 作用 |
|---|---:|---|
| `0x182D91FF0` | `0x2D91FF0` | 创建 `newsync` Task；Task type `138` |
| `0x182D92330` | `0x2D92330` | `newsync` Task 构造函数 |
| `0x181042D10` | `0x1042D10` | 把响应 AutoBuffer 解析到 `Task+0x140` |
| `0x181042D50` | `0x1042D50` | 返回 `Task+0x140` 的响应对象 |
| `0x18065C650` | `0x65C650` | 通用 Protobuf 反序列化 |
| `0x183731D30` | `0x3731D30` | `AddMessageListToDBForSync` |
| `0x181748E00` | `0x1748E00` | `SaveSendMessagesAtOnce` |
| `0x182F99DC0` | `0x2F99DC0` | `ChatMessageStorage` 初始化 |

Task 布局和虚表：

```text
Task + 0xF0   NewSyncRequest       vtable RVA 0x88CFD28
Task + 0x140  NewSyncResponse      vtable RVA 0x88CFDA8
```

反序列化入口：

```text
RCX = destination protobuf object
RDX = 已解密、已解压的 protobuf bytes
R8D = 精确长度
```

按 `NewSyncResponse` 虚表过滤 `0x18065C650` 是观察接收业务明文的首选方式。读取必须限制
在 `R8D` 长度内，不做无界内存转储。

### LongLink 锚点

| IDA VA | RVA | 用途 |
|---|---:|---|
| `0x184A6F75E` | `0x4A6F75E` | 首选发送侧关联点 |
| `0x184BE0B20` | `0x4BE0B20` | 发送封包候选 |
| `0x184BF5140` | `0x4BF5140` | 发送封包候选 |
| `0x184A75DC0` | `0x4A75DC0` | 接收工作函数 |
| `0x184A76748` | `0x4A76748` | 接收日志锚点 |
| `0x184A7698F` | `0x4A7698F` | 接收日志锚点 |

接收日志包含 `cmdid`、`taskid`、`pack_len` 和 `recv_len`。`newsendmsg=522`、
`newsync=138` 是客户端 Task type，不等于旧 XML reqid，也不能直接当作线上 LongLink
`cmdid`。最终映射必须用同一条唯一标记消息在 Protobuf、Task 和 LongLink 三层关联。

## 历史消息接口

| IDA VA | RVA | 接口 |
|---|---:|---|
| `0x183779BE0` | `0x3779BE0` | `CoGetInitialMessageList` |
| `0x18377D5F0` | `0x377D5F0` | `CoGetPagedMessageListWithSortInterval` |
| `0x1837815D0` | `0x37815D0` | `CoGetPagedMessageListWithAnchor` |
| `0x18379AEA0` | `0x379AEA0` | 通用会话分页底层 |
| `0x18376F510` | `0x376F510` | `CoGetInitialMessageListInTimeInterval` |
| `0x183773D80` | `0x3773D80` | `CoGetPagedMessageListInTimeInterval` |

`0x18379AEA0` 不是历史专用函数；发送后的增量刷新也会经过它。判断调用来源必须查看
上层调用栈。已识别的上层候选：

```text
0x1863AB670  LoadSessionWindow50
0x1863B17AC  普通缺失区间加载调用点
0x1863B35A0  更新后的消息刷新
0x1864024D0  resource FTS 后台路径，不是日期跳转首选
```

### 返回容器

普通分页成功路径观察到 `std::shared_ptr<ResultList>`：

```text
shared_ptr + 0x00 -> ResultList
           + 0x08 -> control block

ResultList + 0x00 -> 双向链表哨兵
           + 0x08 -> node count

Node + 0x00 -> next
     + 0x08 -> prev
     + 0x10 -> ChatMessage (size 0x2E0)
```

日期跳转链已动态验证：

```text
0x1819AABE0  DateJump_LoadInitialIntervalTask
  -> 0x18174FF80  Initial interval wrapper
  -> 0x18376F510  CoGetInitialMessageListInTimeInterval
  -> 0x1837726A0  result vector handoff
```

结果是三指针布局的 `std::vector<ChatMessage>`，对象步长 `0x2E0`。一次样本请求 limit 40，
返回 40 条、按时间倒序。日期分页链：

```text
0x1819ABBB0  DateJump_LoadPagedIntervalTask
  -> 0x18178A720
  -> 0x183773D80
  -> 0x183797C5E  paged result vector
```

分页锚点和查询选项：

```text
MessagePageAnchor + 0x00  create_time u32
                  + 0x08  sort_seq u64
                  + 0x10  local_id u32

MessageQueryOptions + 0x00  orientation
                    + 0x04  sort_order
                    + 0x18  types
                    + 0x48  sender (MSVC string)
                    + 0x68  query_more
                    + 0x6C  reserve_content
                    + 0x6D  cancel_last
                    + 0x6E  serial_task
```

格式化函数：`0x181788DE0`、`0x18178A4B0`、`0x18178AEE0`。

## ChatMessage 布局

`ChatMessage` 大小为 `0x2E0`。以下字段来自 ORM 元数据函数 `0x180A63660` 和动态对象
对照：

| 偏移 | 字段 | 类型/说明 |
|---:|---|---|
| `+0x144` | `local_id` | `uint32` |
| `+0x148` | `server_id` | `uint64` |
| `+0x150` | `sort_seq` | `uint64` |
| `+0x158` | `local_type` | `uint32` |
| `+0x160` | `real_sender_id` | `uint32` |
| `+0x164` | `create_time` | `uint32` Unix 秒 |
| `+0x168` | `status` | `uint32` |
| `+0x16C` | `upload_status` | `uint32` |
| `+0x170` | `download_status` | `uint32` |
| `+0x174` | `server_seq` | `uint32` |
| `+0x178` | `origin_source` | 类型待细化 |
| `+0x180` | `message_content` | MSVC `std::string` |
| `+0x1A0` | `compress_content` | MSVC `std::string` |
| `+0x1C0` | `source` | MSVC `std::string` |
| `+0x1E0` | `packed_info_data` | 结构待细化 |

`+0x18`、`+0x38`、`+0x58` 是三个身份/会话字符串，但精确命名仍需用单聊、群聊、收发
四类样本成对验证。

## WCDB 密钥边界

入口：

```text
IDA VA  0x1805DBF40
RVA     0x5DBF40

RCX  WCDB Database/包装对象
RDX  WCDB Data*
R8D  page_size          (动态值 4096)
R9D  compatibility     (动态值 4)

Data + 0x08  key bytes pointer
Data + 0x10  byte length (动态值 32)
```

当前 `chat_plugin` 就 Hook 这个 RVA。最干净的捕获点是函数入口；配置构造器
`0x181465670` 会复制 key，然后对内部副本做 XOR 混淆。

### 跨版本静态恢复

首选锚点不是固定 RVA，而是 WCDB 配置名：

```text
com.Tencent.WCDB.Config.Cipher
```

当前版本中，字符串位于 `0x18892ABC8`，配置注册函数 `0x181463DE0` 将它绑定到全局键
对象 `0x18AFE6E60`。恢复步骤：

1. 搜索配置名字符串并找到注册点；
2. 从 RIP 相对引用解析全局配置键；
3. 搜索 `.text` 中对该全局键的代码引用；
4. 按以下语义筛选：检查 `Data`、构造 `CipherConfig`、安装配置、空值时移除同一配置；
5. 动态复核函数边界、参数顺序、`Data` 布局、页大小和兼容级别。

当前版本函数入口 AOB（已验证单一命中）仅可作为回退：

```text
55 41 57 41 56 56 57 53 48 83 EC 58 48 8D 6C 24 ?? 48 C7 45 ?? ?? ?? ?? ?? 44 89 CF 44 89 C3 49 89 D6
```

内部二次校验片段：

```text
49 83 C7 10 4C 89 F9 4C 89 F2 41 89 D8 41 89 F9 E8 ?? ?? ?? ?? 90
48 8D 15 ?? ?? ?? ?? 4C 8D 45 ?? 48 89 F1 41 B9 00 00 00 80 E8 ?? ?? ?? ??
48 8D 15 ?? ?? ?? ?? 48 89 F1 48 83 C4 58
```

AOB 依赖编译产物，不能单独作为跨版本确认依据。

### 离线验证结论

捕获值是 SQLCipher 的输入，不是最终页密钥。已验证：

```text
salt = database_file[0:16]
page_key = PBKDF2-HMAC-SHA512(captured_key, salt, 256000, dklen=32)
```

把 `page_key` 作为 SQLCipher 原始 hex key 可只读打开并通过 `quick_check`。WCDB API 则可
直接接收捕获的 32 字节输入，并配置 page size 4096、Version4。详细数据库关系见
[DATABASES.md](DATABASES.md)。

## 动态验证流程

1. 记录微信文件版本并重新取得 `Weixin.dll` 基址。
2. 所有断点从 RVA 重算；旧 Live VA 一律丢弃。
3. 初始保持高频 LongLink 断点禁用，只启用一个明确观察点。
4. 让用户手动触发带唯一标记和明确时间的测试行为。
5. 记录线程、调用栈、对象地址和精确长度；输出前移除正文与账号标识。
6. 用数据库行数、时间范围或另一层调用链做交叉验证。
7. 更新地址时同时记录微信版本和证据，不只记录命中结果。

## 未完成项

- 发送入口的完整 ABI、线程/协程约束、对象生命周期与错误回调；
- LongLink 包头、加密/压缩字段和 Task type 到线上 `cmdid` 的最终映射；
- `ChatMessage +0x18/+0x38/+0x58` 三个字符串的精确命名；
- `local_type` 完整枚举和 `packed_info_data` 的结构化解析；
- 非文本消息分支的 Protobuf 与业务对象映射；
- 新微信版本的 RVA、结构、数据库参数和表结构兼容性。
