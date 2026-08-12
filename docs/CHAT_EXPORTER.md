# 聊天导出器

`chat_exporter` 使用 `WCDB::Database` 只读打开数据库，解析单个联系人或群聊，并只把
聊天消息导出为 UTF-8 JSONL。图片、视频、语音和文件等可关联信息会嵌入对应消息，
不会再把联系人、会话、FTS 或业务旁路表分别导出。

## 构建与运行

```powershell
xmake build chat_exporter

$env:WECHAT_DB_KEY_HEX = '<64 位十六进制 WCDB key>'
$env:WECHAT_DB_DIR = (Resolve-Path '.\local-data\db-storage').Path
xmake run chat_exporter -- '<查询值>'
```

完整参数：

```text
chat_exporter <query> [--db-dir <db-storage>] [--key-record <key>]
              [--output <empty-directory>] [--select-username <username>]
```

| 参数 | 说明 |
|---|---|
| `<query>` | 必填；`username`、`alias`、`remark` 或 `nick_name` |
| `--db-dir` | 数据库根目录；默认取 `WECHAT_DB_DIR`，再回退到 `local-data/db-storage` |
| `--key-record` | 直接传入 64 位十六进制 WCDB key；优先于环境变量 |
| `--output` | 输出目录；必须不存在或为空 |
| `--select-username` | 非交互场景从重名候选中选择指定 `username` |

未传 `--key-record` 时必须设置 `WECHAT_DB_KEY_HEX`。命令行参数可能被本机进程检查工具
看到，因此常规使用更推荐环境变量；无论采用哪种方式，都不要把 key 写入脚本或提交。

## 联系人解析

同一个查询值按以下优先级匹配 `contact.db.contact`：

1. `username`：唯一命中后直接选择；
2. `alias`：唯一命中后直接选择；
3. `remark`：可能重名；
4. `nick_name`：可能重名。

同一优先级命中多项时，交互终端显示候选的 `username`、`alias`、`remark` 和
`nick_name` 并要求选择。自动化调用应使用 `--select-username`，且指定值必须属于本次
候选集合。

联系人表无结果时，工具还会检查 `message_0.db.Name2Id` 中的精确会话映射。群聊首先
按 `username` 的 `@chatroom` 后缀判断，并用 `chat_room` 表补充确认。

## 输出目录

默认路径：

```text
local-data/exports/chat_export_<MD5(query)>_<YYYYMMDD_HHMMSS>/
```

目录名只包含查询值的摘要，不泄露联系人名称。为避免混合或覆盖旧数据，指定目录必须
不存在或为空。

消息写入 JSONL，BLOB 使用以下对象保存：

```json
{"encoding":"base64","bytes":123,"data":"..."}
```

`manifest.json` 记录解析后的联系人、会话类型、消息表、输出文件和消息行数，但不包含
数据库 key。程序成功时还会向标准输出写一行摘要 JSON，包括消息数、文件数和绝对输出
路径。

## 导出范围

输出目录只包含：

| 文件 | 内容 |
|---|---|
| `messages.jsonl` | 普通聊天消息，始终生成 |
| `biz_messages.jsonl` | 公众号/业务消息；仅当该会话存在对应分表时生成 |
| `manifest.json` | 会话信息、消息表和文件行数 |

`contact/contact.db` 和 `message/message_0.db` 是核心必需库。资源、语音或 hardlink
数据库缺失时仍会导出消息，只是没有相应附件信息。

## 消息处理

消息表按以下规则定位：

```text
Msg_<MD5(username UTF-8)>
```

工具为 `message_content` 和 `source` 配置 WCDB Zstd 透明解压，按
`sort_seq, local_id` 排序，并添加：

- `local_type_base`：`local_type` 的低 32 位；
- `local_type_flags`：高 32 位标志；
- `real_sender_username`：通过同库 `Name2Id` 解析的真实发送者；
- `create_time_utc`：UTC 时间；
- `message_kind`：按已知基础类型归类为文本、图片、语音、视频、表情、位置、应用消息等；
- `parsed_content` / `parsed_source`：正文是 JSON 或类 XML 时的尽力解析结果；
- `attachments`：按本地/服务端消息 ID 或正文 MD5 关联的媒体信息。

`attachments` 可能包含：

- `message_resource`：资源记录和 detail，包括大小、状态、索引及尚未识别的 packed BLOB；
- `media_0`：语音记录，`data` 中保存 Base64 原始语音 BLOB；
- `hardlink`：从正文 MD5 匹配出的图片、视频或文件名、大小和修改时间。

XML 解析只提取元素文本和属性，不加载外部实体，也不会执行其中的 URL。当前不会把本地
图片或视频文件复制到输出目录，也不会把语音转码为 WAV；无法确认格式的二进制数据按
Base64 原样保留，避免错误解码或数据丢失。

## 失败与验证

常见失败：

- key 不是 64 位十六进制字符串；
- 数据库目录不完整或 key 与快照不匹配；
- 查询无匹配，或非交互重名未指定 `--select-username`；
- 输出目录非空；
- 微信升级导致表或压缩配置变化。

修改导出逻辑后至少验证单聊、群聊、重名联系人、压缩/非压缩正文、图片/语音/视频消息、
可选数据库缺失和空输出目录保护。所有生成物都属于私有数据。
