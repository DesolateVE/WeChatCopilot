# Weixin.dll 聊天收发与持久化逆向分析接力文档

> 更新时间：2026-07-29  
> 分析对象：Windows 微信客户端 `Weixin.exe` / `Weixin.dll`  
> 分析方式：IDA 静态分析 + x64dbg 动态调试  
> 当前目标：定位聊天消息接收、发送、网络协议和本地持久化接口

## 1. 接力摘要

当前版本中，`Weixin.exe` 主要承担启动和模块装载，核心业务位于运行时动态加载的 `Weixin.dll`。

聊天链路已大致确认如下：

```text
发送：
业务层
  -> CoCgiSendRequest
  -> newsendmsg Task
  -> SendMsgRequestNew
  -> Protobuf 序列化
  -> LongLink 封包/加密
  -> Socket

接收：
Socket
  -> LongLink 收包/解密/解压
  -> Task 响应缓冲区
  -> Protobuf ParseFromArray
  -> NewSyncResponse
  -> 内部聊天消息对象
  -> UI 分发和 AddMessageListToDBForSync
  -> 本地数据库
```

当前最有价值的三个切入点：

1. 发送明文：`sub_18065C720`，按 `SendMsgRequestNew` 虚表过滤。
2. 接收明文：`sub_18065C650`，按 `NewSyncResponse` 虚表过滤。
3. 持久化前消息：`AddMessageListToDBForSync`，此处已经是解码后的内部消息对象。

动态调试已经证明：网络消息在进入数据库前已经被转换为包含会话、参与者、正文、类型、时间和 `msgsource` 等字段的内部对象。本文不记录任何实际聊天内容或用户标识。

## 2. 当前调试环境

### 2.1 x64dbg 会话

```text
x64dbg PID:       17916
Debuggee:         Weixin.exe
Debuggee PID:     24692
x64dbg path:      D:\Scoop\apps\x64dbg\current\release\x64\x64dbg.exe
REQ port:         62170
PUB/SUB port:     60332
```

最后确认的 `Weixin.dll` 运行时基址：

```text
Weixin.dll = 0x7FF906020000
```

IDA 数据库使用的镜像基址：

```text
0x180000000
```

地址换算：

```text
RVA      = IDA_VA - 0x180000000
Live_VA  = Weixin.dll_runtime_base + RVA
```

进程重启或 DLL 重新加载后必须重新查询基址，不要直接复用本文的 `Live VA`。

### 2.2 IDA MCP

```text
Endpoint:   http://127.0.0.1:13337/mcp
IDA PID:    3220
Module:     Weixin.dll
```

注意：本文较早章节中标为“当前 Live VA”的地址来自旧运行时基址，仅 RVA
可跨进程复用。2026-07-29 续查所得的最新地址见第 13 节。

### 2.3 用户授权范围

用户已明确允许：

- 新增、启用和禁用上述软件断点；
- 继续运行被调试进程；
- 只读检查寄存器；
- 只读检查网络包和 Protobuf 缓冲区。

未授权且不应执行：

- 修改业务内存；
- 注入代码或 DLL；
- 代替用户发送消息；
- 调用尚未验证的内部发送函数；
- 修改本地聊天数据库。

## 3. 已动态验证的事实

### 3.1 消息持久化入口

`AddMessageListToDBForSync` 已自然命中两次：

```text
IDA VA:   0x183731D30
RVA:      0x3731D30
Live VA:  0x7FF8E3A21D30
```

命中时：

- `R8` 指向消息对象容器；
- 容器中包含一条或多条已经解码的内部聊天消息；
- 能识别会话标识、参与者、正文、消息类型、时间和 `<msgsource>`；
- 证明持久化层接收的不是原始网络包，而是完整业务对象。

这个位置适合：

- 只读导出同步到达的聊天记录；
- 继续识别内部消息类的字段布局；
- 追踪下游数据库表和存储实现。

### 3.2 当前断点状态

最后一次确认/修改后的软件断点：

| Live VA | 名称 | 状态 | 命中 |
|---|---|---:|---:|
| `0x7FF8E094C720` | `PB_Serialize_SendMsg` | 已启用 | 待触发 |
| `0x7FF8E1A38E00` | `SaveSendMessagesAtOnce` | 已禁用 | 0 |
| `0x7FF8E30CE060` | `MicroMsgRequestNew_ctor` | 已禁用 | 0 |
| `0x7FF8E3289DC0` | `ChatMessageStorage_init` | 已禁用 | 0 |
| `0x7FF8E3A21D30` | `AddMessageListToDBForSync` | 已禁用 | 2 |

`PB_Serialize_SendMsg` 已设置并成功接受以下条件：

```text
poi(rcx) == 0x7FF8E8FC5FC8
```

该地址是当前基址下 `SendMsgRequestNew` 的虚表地址。

注意：

- `PB_Parse_NewSync` 接收明文断点尚未设置。
- LongLink 收发日志断点尚未设置。
- 接力 agent 应先调用 `list_breakpoints` 和查询当前暂停/运行状态，不要假设表中状态永久有效。

## 4. 程序架构判断

### 4.1 启动与业务模块

`Weixin.exe` 的静态导入表没有直接出现主要业务 DLL，`Weixin.dll` 应由启动过程动态加载。核心聊天、网络、同步、Protobuf 和存储逻辑均在 `Weixin.dll`。

### 4.2 业务请求层

业务层通过 `kernel::foundation::CoCgiSendRequest` 一类封装创建具体 CGI Task。Task 中内嵌：

- 请求 Protobuf 对象；
- 响应 Protobuf 对象；
- CGI 路径；
- 内部任务类型；
- 完成回调及上下文。

Task 最终被交给网络管理器，而不是由聊天业务代码直接操作 Socket。

### 4.3 Protobuf 层

已识别以下生成类：

- `SendMsgRequestNew`
- `SendMsgResponseNew`
- `NewSyncRequest`
- `NewSyncResponse`
- `MicroMsgRequestNew`
- `SyncKey`

Protobuf 通用序列化和反序列化边界是当前最稳定的明文观察点。

### 4.4 LongLink 层

LongLink 层位于 Protobuf 与 Socket 之间，至少负责：

- `taskid` 和 `cmdid`；
- 包长和已接收长度；
- 包头与分帧；
- 请求和响应的任务关联；
- 加密/解密；
- 可能的压缩/解压。

目前尚未完整恢复：

- LongLink 包头结构；
- `cmdid` 的精确字段偏移；
- 加密和密钥派生函数；
- 压缩标志及算法选择；
- `taskid/cmdid` 与具体 CGI Task 的完整映射。

### 4.5 存储层

接收消息经过 Protobuf 和业务层转换后，再交给 `ChatMessageStorage`。因此有两条持久化研究路线：

1. 在 `AddMessageListToDBForSync` 前读取内部消息对象，绕开数据库格式和数据库加密。
2. 从 `ChatMessageStorage` 继续向下追踪 SQL、表结构、数据库文件和加密层。

若目标只是可靠导出聊天记录，路线 1 通常更稳定。

## 5. 发送消息链路

### 5.1 newsendmsg Task 创建与提交

```text
Function:  sub_183891930
IDA VA:    0x183891930
RVA:       0x3891930
Live VA:   0x7FF8E3B81930
CGI path:  /cgi-bin/micromsg-bin/newsendmsg
Task type: 522
Task size: 344 (0x158)
```

Task 内主要对象：

```text
Task + 0xF0   SendMsgRequestNew
Task + 0x120  SendMsgResponseNew
```

在 IDA `0x183891B35` 附近，通过网络管理器虚函数：

```text
[manager->vtable + 0x28]
```

提交 Task。

相关函数：

| IDA VA | RVA | 作用 |
|---|---:|---|
| `0x18388D040` | `0x388D040` | `kernel::foundation::CoCgiSendRequest` 发送消息相关路径 |
| `0x183891930` | `0x3891930` | 创建并提交 `newsendmsg` Task |
| `0x183891C70` | `0x3891C70` | `newsendmsg` Task 构造函数 |
| `0x18065C720` | `0x65C720` | 通用 Protobuf 序列化 |
| `0x180107570` | `0x107570` | 解码 `SendMsgResponseNew` |
| `0x183891FB0` | `0x3891FB0` | 发送完成业务回调 |

### 5.2 通用 Protobuf 序列化

```text
Function: sub_18065C720
IDA VA:   0x18065C720
RVA:      0x65C720
Live VA:  0x7FF8E094C720
```

入口参数：

```cpp
RCX = protobuf message object
RDX = output string/buffer object
```

函数行为：

- 通过虚表调用 ByteSize，观察到类似 `[vtable + 0x88]`；
- 准备 coded output；
- 通过虚表调用具体序列化实现，观察到类似 `[vtable + 0x72]`。

注意：入口处 `RDX` 指向的输出对象可能尚未装入最终字节。若要读取最终序列化结果：

1. 记录入口时的 `RDX`；
2. 记录 `[RSP]` 返回地址；
3. 使用单次返回断点或 `run_to_return`；
4. 返回后读取先前保存的输出对象。

也可直接在入口读取 `RCX` 中的请求字段。

### 5.3 SendMsgRequestNew 结构

已识别字段：

```text
SendMsgRequestNew
+0x08  repeated MicroMsgRequestNew* 数组
+0x10  元素数量
+0x18  数组容量
+0x20  Count
+0x28  has-bits
```

单条 `MicroMsgRequestNew`：

```text
+0x08  ToUserName
+0x10  Content
+0x18  Type
+0x1C  CreateTime
+0x20  MsgSource
+0x28  ClientMsgId
+0x2C  额外 MsgSource/标志字段，语义待确认
+0x34  has-bits
```

### 5.4 发送相关虚表

| 类型 | IDA 符号 | RVA | 当前 Live VA |
|---|---|---:|---|
| `SendMsgRequestNew` | `off_188CD5FC8` | `0x8CD5FC8` | `0x7FF8E8FC5FC8` |
| `SendMsgResponseNew` | `off_188CD60E8` | `0x8CD60E8` | `0x7FF8E8FC60E8` |

## 6. 接收和同步消息链路

### 6.1 newsync Task

```text
Function:  sub_182D91FF0
IDA VA:    0x182D91FF0
RVA:       0x2D91FF0
Live VA:   0x7FF8E3081FF0
CGI path:  /cgi-bin/micromsg-bin/newsync
Task type: 138
Task size: 376 (0x178)
```

Task 内主要对象：

```text
Task + 0xF0   NewSyncRequest
Task + 0x140  NewSyncResponse
```

相关函数：

| IDA VA | RVA | 作用 |
|---|---:|---|
| `0x182D91FF0` | `0x2D91FF0` | 创建 `newsync` Task |
| `0x182D92330` | `0x2D92330` | `newsync` Task 构造函数 |
| `0x181042D10` | `0x1042D10` | 将响应 AutoBuffer 解析至 `Task + 0x140` |
| `0x181042D50` | `0x1042D50` | 返回 `Task + 0x140` 的响应对象 |
| `0x18065C650` | `0x65C650` | 通用 Protobuf `ParseFromArray` |

### 6.2 通用 Protobuf 反序列化

```text
Function: sub_18065C650
IDA VA:   0x18065C650
RVA:      0x65C650
Live VA:  0x7FF8E094C650
```

入口参数：

```cpp
RCX  = destination protobuf object
RDX  = plaintext protobuf byte buffer
R8D  = buffer length
```

此时 `RDX` 中是经过底层解密/解压后的业务明文，是分析接收协议的最佳位置之一。

建议条件：

```text
poi(rcx) == NewSyncResponse_vtable
```

如果也需要观察发送消息的服务端确认响应，可增加：

```text
poi(rcx) == NewSyncResponse_vtable ||
poi(rcx) == SendMsgResponseNew_vtable
```

当前基址下可使用：

```text
poi(rcx)==0x7FF8E8BBFDA8 || poi(rcx)==0x7FF8E8FC60E8
```

### 6.3 NewSync 虚表

| 类型 | IDA 符号 | RVA | 当前 Live VA |
|---|---|---:|---|
| `NewSyncRequest` | `off_1888CFD28` | `0x88CFD28` | `0x7FF8E8BBFD28` |
| `NewSyncResponse` | `off_1888CFDA8` | `0x88CFDA8` | `0x7FF8E8BBFDA8` |

类型字符串静态地址：

```text
NewSyncRequest:  0x1888CFD88
NewSyncResponse: 0x1888CFE08
SyncKey:         0x1888CFF08
```

相关构造函数：

```text
NewSyncRequest:  sub_181060960
NewSyncResponse: sub_1810609A0
```

## 7. LongLink 收发层观察点

### 7.1 发送侧

发送日志/封包锚点：

| IDA VA | RVA | 当前 Live VA |
|---|---:|---|
| `0x184A6F75E` | `0x4A6F75E` | `0x7FF8E4D5F75E` |
| `0x184BE0B20` | `0x4BE0B20` | `0x7FF8E4ED0B20` |
| `0x184BF5140` | `0x4BF5140` | `0x7FF8E4EE5140` |

首选先观察 `0x184A6F75E`。其所在函数为 `sub_184A6DBB0`。该层可用于关联：

```text
SendMsgRequestNew
  <-> Task 指针
  <-> taskid/cmdid
  <-> 最终 LongLink 包
```

由于 LongLink 可能有大量后台心跳和同步请求，不建议一开始无条件启用所有发送锚点。

### 7.2 接收侧

```text
Receive worker:
IDA VA:   0x184A75DC0
RVA:      0x4A75DC0
Live VA:  0x7FF8E4D65DC0
```

接收日志锚点：

| IDA VA | RVA | 当前 Live VA |
|---|---:|---|
| `0x184A76748` | `0x4A76748` | `0x7FF8E4D66748` |
| `0x184A7698F` | `0x4A7698F` | `0x7FF8E4D6698F` |

日志格式字符串：

```text
index:%_, sock:%_, %_, ret:%_, cmdid:%_, taskid:%_, pack_len:%_, recv_len:%_
```

在 `0x184A76748` 附近，日志参数指针数组位于：

```text
RBP + 0x320
```

观察到的指针槽：

```text
RBP+0x320
RBP+0x328
RBP+0x330
RBP+0x338
RBP+0x340
RBP+0x348
RBP+0x350
RBP+0x358  （最后一项来自 r15）
```

命中后应：

1. 读取全部寄存器；
2. 读取 `RBP+0x320` 起的 64 字节；
3. 依次解引用各参数指针；
4. 映射 `cmdid/taskid/pack_len/recv_len`；
5. 再与随后命中的 `NewSyncResponse` Parse 断点关联。

## 8. 持久化相关函数

| IDA VA | RVA | 当前 Live VA | 作用 |
|---|---:|---|---|
| `0x183731D30` | `0x3731D30` | `0x7FF8E3A21D30` | `AddMessageListToDBForSync` |
| `0x181748E00` | `0x1748E00` | `0x7FF8E1A38E00` | `SaveSendMessagesAtOnce` |
| `0x182F99DC0` | `0x2F99DC0` | `0x7FF8E3289DC0` | `ChatMessageStorage` 初始化 |

建议从 `AddMessageListToDBForSync` 继续向下：

- 找最终存储虚函数；
- 找 SQL 字符串或 ORM/Storage 封装；
- 识别数据库句柄；
- 识别消息表和列映射；
- 判断加密发生在 SQLite 层、VFS 层还是字段层。

不要把数据库写入函数当作网络接收入口；它已经处于业务链路末端。

## 9. 任务类型、reqid 与 cmdid 的区别

程序内旧 XML 配置可看到：

```text
newsendmsg reqid = 237
newsendmsg respid = 1000000237
newsync reqid = 121
```

但当前代码创建 Task 时观察到：

```text
newsendmsg task type = 522
newsync task type    = 138
```

这些值不能直接视为线上 LongLink `cmdid`：

- XML 可能属于旧版本或兼容配置；
- Task type 可能是客户端内部路由编号；
- LongLink `cmdid` 可能由网络层再次映射。

必须动态关联一次完整请求才能下结论。

## 10. 推荐的下一步动态操作

### 10.1 恢复会话并确认基址

使用 x64dbg MCP：

```text
1. list_sessions
2. connect_to_session(session_pid=4524)
3. eval_expression("Weixin.dll")
4. list_breakpoints("software")
```

如果基址变化，按 RVA 重算所有地址和虚表条件。

### 10.2 设置接收明文断点

当前基址未变化时：

```text
BP address:
0x7FF8E094C650

Condition:
poi(rcx)==0x7FF8E8BBFDA8 || poi(rcx)==0x7FF8E8FC60E8
```

建议名称：

```text
PB_Parse_NewSync_SendResp
```

可通过：

```text
SetBreakpointCondition 0x7FF8E094C650, poi(rcx)==0x7FF8E8BBFDA8 || poi(rcx)==0x7FF8E8FC60E8
```

设置条件。

### 10.3 触发测试

让用户手动发送一条具有明显标记的普通文本消息。调试端不要主动发送。

发送序列化断点命中后记录：

```text
RCX  请求对象
RDX  输出对象
RSP  返回地址位置
线程 ID
调用栈
```

读取：

- `[RCX]` 验证虚表；
- `SendMsgRequestNew + 0x08/+0x10/+0x18`；
- repeated 数组中的 `MicroMsgRequestNew*`；
- 单条消息对象的已知字段。

输出日志或文档时应对正文和用户标识脱敏。

### 10.4 观察接收明文

`sub_18065C650` 命中时记录：

```text
RCX = NewSyncResponse 或 SendMsgResponseNew
RDX = 明文 protobuf bytes
R8D = 精确长度
```

只读取 `R8D` 指定的长度。若包很大：

- 先保存前 4 KiB；
- 同时记录完整长度；
- 必要时分块读取；
- 不要无界读取内存区域。

返回后可再次检查先前保存的 `RCX`，确认生成对象字段。

### 10.5 关联 LongLink

确认发送明文命中后，再临时启用：

```text
Send:    0x7FF8E4D5F75E
Receive: 0x7FF8E4D66748
```

目标是建立：

```text
marker text
  -> SendMsgRequestNew
  -> newsendmsg Task
  -> taskid/cmdid
  -> LongLink frame
  -> SendMsgResponseNew
  -> NewSyncResponse
  -> AddMessageListToDBForSync
```

完成这条关联后，才能可靠描述 LongLink 协议字段。

## 11. 尚未完成的问题

- `SendMsgRequestNew` 的完整字段编号与所有权规则；
- 微信内部字符串类的统一解析方式；
- `NewSyncResponse` 中消息列表的精确字段布局；
- 消息对象从 Protobuf 到内部 ChatMessage 的转换函数；
- LongLink 包头格式；
- 加密、解密和压缩函数；
- 实际线上 `cmdid`；
- Task 提交和回调的线程约束；
- 能安全复用的高层发送 ABI；
- 本地数据库路径、表结构和加密方式；
- 撤回、图片、文件、语音和群消息等非纯文本消息的分支协议。

## 12. 结论与接口选择建议

### 只读接收聊天

首选：

```text
NewSyncResponse Parse 完成后
```

或：

```text
AddMessageListToDBForSync 入口
```

前者更接近网络协议，后者更接近最终业务消息。

### 只读持久化聊天

首选在 `AddMessageListToDBForSync` 前复制必要字段到独立存储。这样不依赖微信数据库结构和数据库密钥。

### 发送聊天

从架构上应复用：

```text
CoCgiSendRequest
  -> newsendmsg Task
  -> SendMsgRequestNew
```

不要从 Socket 层伪造数据包。但当前尚未验证对象构造、内存所有权、线程和回调 ABI，因此还没有达到安全调用内部发送接口的程度。

### 协议分析

以 `sub_18065C720` 和 `sub_18065C650` 获取业务 Protobuf，以 LongLink 日志锚点获取 `taskid/cmdid` 和包长，再通过同一次标记消息做时间与线程关联。这是下一阶段最短路径。

## 13. 2026-07-29：历史聊天记录读取续查

### 13.1 已定位的本地数据库

当前版本的数据根目录为：

```text
%USERPROFILE%\Documents\xwechat_files\<account_id>\db_storage
```

与聊天记录直接相关的文件：

```text
message\message_0.db
message\message_0.db-wal
message\message_0.db-shm
message\message_fts.db
message\message_resource.db
session\session.db
```

其中：

- `message_0.db` 是主要消息库；
- `message_fts.db` 是消息全文检索库；
- `message_resource.db` 保存消息资源关联信息；
- `session.db` 保存会话级数据；
- `biz_message_0.db` 是公众号/业务消息库。

对 `message_0.db`、`message_fts.db` 和 `session.db` 的首页做了只读检查，
均不以 `SQLite format 3` 开头。`Weixin.dll` 内同时静态包含 WCDB 和
SQLCipher 相关实现及 `com.Tencent.WCDB.Config.Cipher`，因此当前判断为
WCDB/SQLCipher 页级加密，而不是字段级明文 SQLite。

不要直接在运行中的原库上尝试写操作。即使取得密钥，离线读取也应复制
主库、`-wal` 和 `-shm` 后再处理，以免遗漏尚未 checkpoint 的消息。

### 13.2 已定位的历史消息高层读取接口

字符串交叉引用确认以下函数属于 `ChatMessageStorage` 的历史记录查询链：

| IDA VA | RVA | 当前 Live VA | 作用 |
|---|---:|---|---|
| `0x183779BE0` | `0x3779BE0` | `0x7FF909799BE0` | `CoGetInitialMessageList` |
| `0x18377D5F0` | `0x377D5F0` | `0x7FF90979D5F0` | `CoGetPagedMessageListWithSortInterval` |
| `0x1837815D0` | `0x37815D0` | `0x7FF9097A15D0` | `CoGetPagedMessageListWithAnchor` |
| `0x18379AEA0` | `0x379AEA0` | `0x7FF9097BAEA0` | `CoGetSessionMessageListWithPageFromDB` |
| `0x18376F510` | `0x376F510` | `0x7FF90978F510` | `CoGetInitialMessageListInTimeInterval` |
| `0x183773D80` | `0x3773D80` | `0x7FF909793D80` | `CoGetPagedMessageListInTimeInterval` |

这组接口比 `AddMessageListToDBForSync` 更适合导出历史记录：

```text
UI 打开会话/向上翻页
  -> CoGetInitialMessageList / CoGetPagedMessageList...
  -> WCDB 查询
  -> 已解密、已反序列化的 ChatMessage 列表
  -> UI ViewModel
```

初步反编译结果：

- `CoGetInitialMessageList` 有 6 个参数；
- 第三个参数是会话字符串对象，函数会先将其映射为内部 session id；
- 第四个参数是非零分页/索引参数；
- `CoGetSessionMessageListWithPageFromDB` 有 7 个参数，包含会话对象、两个
  32 位分页参数及结果/回调对象；
- 函数使用协程/回调包装，入口返回值不是最终消息数组，需继续追踪完成回调。

### 13.3 当前动态断点

当前已启用：

| Live VA | 名称 | 状态 | 命中 |
|---|---|---:|---:|
| `0x7FF90C3CB670` | `History_LoadSessionWindow50` | 已启用 | 0 |
| `0x7FF90C3D17AC` | `History_NormalMissingRange_Callsite` | 已启用 | 0 |
| `0x7FF90776FF80` | `History_TimeInterval_Initial_Wrapper` | 已启用 | 0 |
| `0x7FF9077AA720` | `History_TimeInterval_Paged_Wrapper` | 已启用 | 0 |
| `0x7FF90978F510` | `History_InitialMessageList_TimeInterval` | 已启用 | 0 |
| `0x7FF909793D80` | `History_PagedMessageList_TimeInterval` | 已启用 | 0 |

通用结果断点、此前网络发送断点和消息同步写入断点目前均已禁用，
以免发送后的增量刷新被误当成历史分页。

下一次动态操作：

1. 用户在微信 UI 中向上滚动，触发缺失区间加载；
2. 用户另行执行一次按日期定位，测试 create-time/resource-FTS 路径；
3. 根据命中的上层入口区分普通滚动、日期检索和发送后增量刷新；
4. 在正确上层路径中恢复结果断点，读取链表但不修改进程内存；
5. 实现只读批量导出器。

### 13.4 两条可行提取路线

路线 A（当前首选）：

```text
挂钩 CoGetInitialMessageList / CoGetPagedMessageList...
  -> 读取微信自身返回的 ChatMessage 列表
  -> 导出 JSONL/SQLite
```

优点是无需恢复 WCDB 密钥和页参数，且 WAL 合并由微信自身完成。

路线 B：

```text
追踪 WCDB Cipher 配置
  -> 恢复运行时数据库 key、page size、KDF/HMAC 参数
  -> 复制 db + wal + shm
  -> 离线解密和查询
```

路线 B 更适合全量、无 UI 的离线导出，但当前尚未定位密钥设置调用点。

### 13.5 首次动态命中与结果容器布局

`CoGetInitialMessageList` 首次动态命中时确认：

- `RCX`：`ChatMessageStorage this`；
- `RDX`：输出/异步状态对象；
- `R8`：MSVC 字符串形式的会话 id；
- `R9`：请求数量，本次为 30；
- 第五、第六参数分别位于 `[RSP+0x28]`、`[RSP+0x30]`。

该入口很快返回异步结果句柄，入口返回值不是消息数组。随后命中
`CoGetSessionMessageListWithPageFromDB`：

- `R8D`：方向/模式参数；
- `R9`：会话字符串对象；
- `[RSP+0x28]`：锚点/索引；
- `[RSP+0x30]`：底层批量大小，本次为 1000；
- `[RSP+0x38]`：查询选项或取消状态。

静态反编译与动态对象地址已互相验证底层成功结果的布局。成功路径在：

```text
IDA:  0x18379C7C5
Live: 0x7FF9097BC7C5
```

此处 `RDX` 指向 `std::shared_ptr<ResultList>`：

```text
RDX + 0x00 -> ResultList 对象指针
RDX + 0x08 -> shared_ptr 控制块

ResultList + 0x00 -> 双向链表哨兵节点
ResultList + 0x08 -> 节点数量

Node + 0x00 -> next
Node + 0x08 -> prev
Node + 0x10 -> ChatMessage（大小 0x2E0 / 736 字节）
```

构造路径的关键证据：

- `sub_1837ED520` 为链表哨兵分配 752 字节；
- `sub_18151E910` 将数据库结果转换成该消息链表；
- 成功时创建 32 字节 shared_ptr 控制块，并把哨兵指针和节点数放入
  对象的前 16 字节；
- `sub_1837EE4A0` 仅包装并转交这个 shared_ptr，不再复制消息正文。

该观察点能够读取微信已经解密并反序列化的对象，不需要恢复 WCDB
密钥，也不需要修改原数据库。但后续动态验证发现它是公共底层，发送
新消息后的增量刷新也会经过这里，不能仅凭该断点命中判断“历史分页”。

### 13.6 纠正：公共分页底层与真实上层来源

发送一条新消息时，`0x7FF9097BC7C5` 再次命中，返回链表节点数为 1，
节点内容与刚发送的消息一致。完整返回地址回溯确认本次调用链为：

```text
RE_RefreshChangedMessages_AfterUpdate
  0x1863B35A0 / live 0x7FF90C3D35A0
    -> RE_LoadSessionRange1000
       0x1863B1950 / live 0x7FF90C3D1950
    -> RE_GetSessionMessageListWithPage_BySession
       0x181788090 / live 0x7FF9077A8090
    -> RE_GetSessionMessageListWithPage_ByLocalId
       0x181788290 / live 0x7FF9077A8290
    -> RE_GetSessionMessageListWithPage_Common
       0x18379AEA0 / live 0x7FF9097BAEA0
```

因此 `sub_18379AEA0` 应理解为通用会话分页读取，不是历史专用函数。
本次发送后的上层调用点是 IDA `0x1863B3928`；普通缺失区间加载的独立
调用点是 IDA `0x1863B17AC`，已作为向上滚动候选断点。

另外定位到两个更高层的路径：

```text
RE_LoadSessionWindow50
  IDA  0x1863AB670
  live 0x7FF90C3CB670
  特征：请求 0x32（50）条消息

RE_ResourceFTS_GetSessionMessagesOnCreateTime
  IDA  0x1864024D0
  live 0x7FF90C4204D0
  特征：包含 GetSessionMessageListWithPageOnCreateTime 和 resource FTS 日志
```

前者是会话窗口加载候选。后者最初被当作按日期查询候选，但实际执行
一次日期跳转并未命中；由于日志明确带有 `resource FTS`，当前下调为
资源索引后台路径，不再作为聊天日期跳转的首选观察点。

### 13.7 ChatMessage 已确认字段偏移

`RE_ChatMessage_ORM_Metadata`（IDA `0x180A63660`）直接注册数据库列名
与对象成员偏移。它与当前断下的 736 字节对象内容完全一致，已确认：

| ChatMessage 偏移 | 字段 | 类型/说明 |
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
| `+0x178` | `origin_source` | 待细化类型 |
| `+0x180` | `message_content` | MSVC `std::string` |
| `+0x1A0` | `compress_content` | MSVC `std::string` |
| `+0x1C0` | `source` | MSVC `std::string`，常见为 msgsource XML |
| `+0x1E0` | `packed_info_data` | 待细化结构 |

当前消息还观察到 `+0x18`、`+0x38`、`+0x58` 为三个字符串成员，其中
包含发送者、会话和实际发送者标识；其精确语义需用单聊、群聊收发消息
各对照一次后再命名。

### 13.8 日期跳转的时间区间 API

在 `resource FTS` 候选没有命中后，重新启用此前已静态定位、但尚未做
动态验证的两组明确时间区间接口：

```text
RE_GetInitialMessageListInTimeInterval_Wrapper
  IDA  0x18174FF80
  live 0x7FF90776FF80

RE_GetPagedMessageListInTimeInterval_Wrapper
  IDA  0x18178A720
  live 0x7FF9077AA720

RE_CoGetInitialMessageListInTimeInterval
  IDA  0x18376F510
  live 0x7FF90978F510

RE_CoGetPagedMessageListInTimeInterval
  IDA  0x183773D80
  live 0x7FF909793D80
```

两条底层函数都包含明确的函数名和耗时日志，参数日志还包含：

```text
session
index / last
limit
orientation
ext_param
```

它们同样通过 `sub_1837DB1F0` 返回 0x2E0 字节步长的消息向量。下一次
日期跳转应先观察 wrapper 与底层入口是否命中；若仍不命中，则日期 UI
使用的是另一套索引/IPC 路由，需要从 UI 事件或任务分发表反向追踪。

### 13.9 日期区间动态验证成功

第二次执行按日期跳转时，真实调用链成功命中：

```text
RE_DateJump_LoadInitialIntervalTask
  IDA  0x1819AABE0
  live 0x7FF9079CABE0
    -> RE_GetInitialMessageListInTimeInterval_Wrapper
       IDA  0x18174FF80
       live 0x7FF90776FF80
    -> RE_CoGetInitialMessageListInTimeInterval
       IDA  0x18376F510
       live 0x7FF90978F510
    -> result vector handoff
       IDA  0x1837726A0
       live 0x7FF9097926A0
```

本次入口参数：

```text
session: MSVC std::string
interval.from: 所选日期当天 00:00 的 Unix 秒
interval.to:   0xFFFFFFFF
limit:         0x28（40）
```

成功点 `RDX` 指向：

```text
std::vector<ChatMessage> {
  begin;
  end;
  capacity_end;
}
```

动态计算：

```text
(end - begin) / 0x2E0 = 0x28
```

即一次返回连续 40 条消息。首尾 `create_time` 均不早于
`interval.from`，向量按时间倒序。包装层返回后，调用者输出对象仍为
同样的 vector 三指针布局。

分页对应的已确认静态路径：

```text
RE_DateJump_LoadPagedIntervalTask
  IDA  0x1819ABBB0
  live 0x7FF9079CBBB0
    -> RE_GetPagedMessageListInTimeInterval_Wrapper
       live 0x7FF9077AA720
    -> RE_CoGetPagedMessageListInTimeInterval
       live 0x7FF909793D80
    -> paged result vector
       live 0x7FF909797C5E
```

分页任务结构比初始任务多一个 `last/anchor` 字段：

```text
task + 0x08: session
task + 0x28: interval {from, to}
task + 0x30: last/anchor
task + 0x48: limit
task + 0x50: query options
```

### 13.10 只读向量导出器

新增：

```text
tools/debugger/extract_chatmessage_vector.py
```

脚本通过已安装的 `x64dbg_automate` 连接现有调试会话，只使用寄存器求值
和内存读取接口。它不会写入调试进程，也不会访问或修改微信数据库。

在结果向量断点处可这样导出：

```powershell
python .\tools\debugger\extract_chatmessage_vector.py `
  --session-pid 17916 `
  --vector-register rdx `
  --output artifacts\exports\date_query_capture.jsonl
```

如果已经返回到包装层，且 `RAX` 指向 vector 对象，则可将参数改为：

```text
--vector-register rax
```

当前日期查询的 40 条结果已成功导出并通过 JSON 解析、条数、首尾索引及
时间范围校验：

```text
artifacts\exports\date_query_capture.jsonl
```

导出字段包括三个身份/会话字符串、local/server id、sort_seq、类型、
create_time、状态、正文、压缩正文和 source。`+0x18` 与 `+0x58` 两个
身份字符串仍使用中性字段名，等待收发/单群聊样本进一步确认语义。

### 13.11 分页预取与自动捕获

用户报告向上滚动未看到暂停，但断点命中计数表明日期分页已经实际执行：

```text
History_DateJump_PagedTask:             2
History_TimeInterval_Paged_Wrapper:     3
History_PagedMessageList_TimeInterval:  3
History_TimeInterval_Paged_ResultVector: 2
```

这说明日期跳转后存在自动预取，不能用 x64dbg 窗口是否长时间停住来判断
分页是否发生。初始向量按时间倒序，并以所选日期 00:00 为下界；UI 很可能
锚定最早一条，因此向上越过下界不会继续取同一区间，向下到较新边界更
容易触发下一页。

新增只读自动监视器：

```text
tools/debugger/monitor_chatmessage_vectors.py
```

当前同时保留首次结果向量断点 `0x7FF9097926A0` 和分页结果向量断点
`0x7FF909797C5E`，其他中间断点已禁用。监视器在这两个白名单断点暂停时
读取 `RDX` 向量、按
`(session, local_id, server_id, sort_seq)` 去重追加 JSONL，然后自动
恢复微信。它不会处理或自动恢复任何非白名单断点。

本次监视器参数：

```text
session pid: 17916
initial result: 0x7FF9097926A0
paged result:   0x7FF909797C5E
output:      artifacts\exports\date_query_capture.jsonl
idle timeout: 900 秒
```

日志：

```text
artifacts\logs\chat_vector_monitor_dual.out.log
artifacts\logs\chat_vector_monitor_dual.err.log
```

重新执行一次日期查询后，两处断点均各新增一次命中。监视器成功捕获：

```text
initial result: 40 条
paged result:    1 条
new records:    41 条
JSONL total:    81 条
```

这 41 条属于同一个会话。首次结果的时间范围是
`2026-07-15 01:16:15` 至 `2026-07-15 01:43:03`（本地时间）；分页结果
是 `2026-07-14 23:30:34` 的 1 条边界记录。该样本说明日期跳转会先返回
从目标日期开始的最多 40 条消息，然后分页路径可能补取目标日期下界之前
的一条上下文/锚点记录。此前滚动未新增断点命中，是因为 UI 正在消费已经
预取的结果，而不是再次访问数据库。

每条由监视器新增的 JSONL 记录都带有 `capture_batch` 和
`capture_breakpoint`；监视器日志中的 `captured` 事件也会记录
`breakpoint`，可直接区分首次结果与分页结果。

### 13.12 动态确认分页锚点与查询选项

在 `RE_DateJump_LoadPagedIntervalTask`（IDA `0x1819ABBB0`，live
`0x7FF9079CBBB0`）入口读取任务对象，确认 `task + 0x30` 不是完整
`ChatMessage`，而是 24 字节的轻量分页锚点：

```text
MessagePageAnchor + 0x00: create_time u32
MessagePageAnchor + 0x04: reserved u32
MessagePageAnchor + 0x08: sort_seq u64
MessagePageAnchor + 0x10: local_id u32
MessagePageAnchor + 0x14: reserved u32
```

动态样本的 `create_time`、`sort_seq` 和 `local_id` 与首次 40 条结果中
时间最早的那条消息逐项一致。

同一次分页任务的参数为：

```text
interval.from = 0
interval.to   = 0xFFFFFFFF
limit         = 1
```

因此日期跳转后的这次分页不是普通的 40 条下一页，而是以首次结果中最早的
消息为锚点，在日期下界之前补取 1 条上下文记录。这也解释了结果中仅有一条
前一日消息。

`task + 0x50` 的查询选项由格式化函数进一步确认：

```text
MessageQueryOptions + 0x00: orientation
MessageQueryOptions + 0x04: sort_order
MessageQueryOptions + 0x18: types
MessageQueryOptions + 0x48: sender (MSVC string)
MessageQueryOptions + 0x68: query_more
MessageQueryOptions + 0x6C: reserve_content
MessageQueryOptions + 0x6D: cancel_last
MessageQueryOptions + 0x6E: serial_task
```

本次动态值：

```text
orientation=0, sort_order=1, types=0, sender=""
query_more=0xFFFFFFFF
reserve_content=true, cancel_last=false, serial_task=false
```

IDA 新增命名：

```text
0x181788DE0 RE_FormatMessageQueryOptions
0x18178A4B0 RE_FormatTimeInterval
0x18178AEE0 RE_FormatMessagePageAnchor
```

x64dbg 对应 live 地址也已设置同名标签，并在分页任务入口写入完整布局备注。

### 13.13 WCDB 明文密钥边界

为全量离线读取数据库继续追踪 `com.Tencent.WCDB.Config.Cipher`，已定位：

```text
IDA  0x1805DBF40 RE_WCDB_Database_SetCipherKey
live 0x7FF9065FBF40
```

入口 ABI：

```text
RCX  WCDB Database/数据库包装对象
RDX  WCDB Data*
R8D  page_size
R9D  cipher_compatibility

Data + 0x08: 明文密钥字节指针
Data + 0x10: 密钥字节长度
```

当 `Data` 为空时，该函数移除 `com.Tencent.WCDB.Config.Cipher`；非空时创建
Cipher 配置。已检查的业务层调用均传入：

```text
page_size = 0x1000
cipher_compatibility = 3
```

配置构造器：

```text
IDA  0x181465670 RE_WCDB_CipherConfig_CtorObfuscateKey
live 0x7FF907485670
```

它先复制传入的明文 `Data`，将 `page_size` 保存到配置对象 `+0x220`、
兼容级别保存到 `+0x224`，随后使用静态 32 字节掩码对配置对象中的密钥
副本做 XOR。因此最干净的动态捕获点是
`RE_WCDB_Database_SetCipherKey` 入口，而不是构造器返回后。

新增只读辅助脚本：

```text
tools/debugger/extract_wcdb_cipher_key.py
```

脚本只接受停在上述白名单入口的调试会话，从 `RDX` 的 WCDB `Data`
对象读取明文密钥，单独写入：

```text
artifacts\exports\wcdb_cipher_key.json
```

控制台仅输出密钥长度、SHA-256 指纹、页大小、兼容级别和目标文件路径，
不打印明文密钥。该 JSON 包含 `key_hex`，应按敏感文件处理，不要提交到
版本控制或发送给无关人员。

x64dbg 已创建并禁用断点：

```text
0x7FF9065FBF40 WCDB_SetCipherKey_PlaintextBoundary
```

该配置通常在数据库首次打开时执行。当前微信已经完成主要数据库初始化，
所以需要在下次受控重启前启用断点，逐次记录命中并结合调用栈/数据库路径
确认哪一次对应 `message_0.db`。

### 13.14 WCDB 消息库密钥动态确认与离线打开

受控重启后，`Weixin.dll` 新基址为：

```text
0x7FF906080000
```

因此 `RE_WCDB_Database_SetCipherKey` 的本次 live 地址为：

```text
0x7FF90665BF40
```

断点命中和进程文件句柄的对应关系：

```text
hit 1  key_info.db
hit 2  contact.db
hit 3  contact.db 仍在初始化
hit 4  message_0.db / message_0.db-wal / message_0.db-shm
```

四次观察到的密钥均为 32 字节；已保存命中 4 的记录：

```text
artifacts\exports\wcdb_cipher_key_message_0.json
```

该文件的密钥 SHA-256 指纹为：

```text
f9396ff77b1b25c6c751e2861a5e52d959c265982019ed0170aeb055a922804e
```

实际运行时参数为：

```text
page_size = 4096
cipher_compatibility = 4
```

动态值 `4` 优先于此前对部分静态调用点观察到的 `3`。

暂停在 hit 4 时已复制一致的只读分析快照：

```text
artifacts\exports\offline_message_0_snapshot_20260730\
  message_0.db
  message_0.db-wal
  message_0.db-shm
```

重要：断点捕获的是 WCDB 传给 `sqlite3_key()` 的 32 字节二进制输入，
不是可以直接通过 SQLCipher `x'...'` 形式使用的最终页密钥。直接将捕获值
作为 hex blob 会在页 1 出现 HMAC 校验失败。

SQLCipher 4 的已验证派生过程：

```text
salt = message_0.db 文件前 16 字节
page_key = PBKDF2-HMAC-SHA512(
    password=captured_key,
    salt=salt,
    iterations=256000,
    dklen=32,
)
```

将 `page_key` 以 SQLCipher 原始 hex key 形式设置后，副本只读打开成功：

```text
SQLCipher: 4.12.0 community
table_count: 153
PRAGMA quick_check: ok
```

新增可复现的只读验证脚本：

```text
tools/offline/verify_wcdb_offline.py
```

本机验证使用仓库私有依赖：

```powershell
python -m pip install --target .\.tools\python sqlcipher3-wheels==0.5.7
$env:PYTHONPATH=(Resolve-Path '.\.tools\python').Path
python .\tools\offline\verify_wcdb_offline.py `
  .\artifacts\exports\offline_message_0_snapshot_20260730\message_0.db
```

`.tools`、密钥 JSON 和数据库快照均已加入 `.gitignore`。密钥记录仍应视为
敏感文件，不要提交或发送给无关人员。

进一步只读检查确认数据库包含真实聊天数据结构：

```text
Msg_* 分表数量: 148
Name2Id 行数: 1138
```

`Msg_*` 表已确认包含 `local_id`、`server_id`、`local_type`、`sort_seq`、
`real_sender_id`、`create_time`、`message_content`、`compress_content` 等字段。
因此下一阶段不再是密码学问题，而是恢复 `Name2Id`/消息表散列映射，并将
148 张消息分表统一导出。

### 13.15 消息分表映射、WCDB 解压与完整导出

`Name2Id` 与全部 `Msg_*` 表的关系已验证：

```text
table_name = "Msg_" + MD5(user_name UTF-8).hexdigest()
```

验证结果：

```text
Name2Id 总行数:                  1138
Name2Id is_session != 0:          148
Msg_* 表:                         148
成功映射:                         148
未映射表:                           0
```

消息的 `real_sender_id` 对应 `Name2Id.rowid`。全部 180606 条消息的
发送者 ID 均成功映射，没有缺失。

WCDB 压缩列标志也已确认：

```text
WCDB_CT_message_content = 4  -> Zstd BLOB
WCDB_CT_source = 4           -> Zstd BLOB
```

压缩数据以标准 Zstd magic `28 B5 2F FD` 开始，无需外部字典即可解压为
UTF-8。统计：

```text
压缩 message_content:  48942
压缩 source:          176552
```

新增完整导出脚本：

```text
tools/offline/export_weixin_messages.py
```

它以只读方式打开副本，统一执行：

```text
Name2Id -> Msg_<md5> 会话映射
Name2Id.rowid -> real_sender_id 发送者映射
WCDB Zstd 列解压
local_type 低 32 位基础类型拆分
按 sort_seq 全局排序
流式 JSONL 输出
```

依赖已记录在：

```text
requirements/offline.txt
```

完整导出已完成：

```text
artifacts\exports\offline_message_0_messages.jsonl
rows:                  180606
conversations:             148
invalid JSON lines:          0
chronological order:      true
size:                287867828 bytes
SHA-256:
86ca7071f03e9c3ce2a19f20228e9e52b0f04fc80d712483ff04a267b1c9e202
```

数据库底层、初始化、读写、WAL、加密、分表和压缩流程另见：

```text
docs/WEIXIN_DATABASE_WORKFLOW.md
```
