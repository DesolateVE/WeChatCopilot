# 聊天导出器

`chat_exporter` 使用 `WCDB::Database` 只读打开数据库，解析单个联系人或群聊，并只把
聊天消息导出为 UTF-8 JSONL。图片、视频、语音和文件等可关联信息会嵌入对应消息，
不会再把联系人、会话、FTS 或业务旁路表分别导出。

## 构建与运行

```powershell
xmake build chat_exporter
xmake run chat_exporter -- 'C:\Users\Administrator\Documents\xwechat_files\wxid_xxx_0000'
```

程序只接收一个路径参数，不再接收联系人、key、输出目录等参数，也不读取
`WECHAT_DB_KEY_HEX` 或 `WECHAT_DB_DIR`。传入路径可以是账号数据根目录，也可以是精确的
`db_storage` 目录。程序先检查直属的 `db_storage/`，必要时向下有限递归查找，并要求其中
同时存在 `contact/contact.db` 和 `message/message_0.db`。启动顺序：

1. 用 `chat_launcher` 启动微信，等待 `chat_plugin` 捕获数据库 key；
2. 把账号目录作为唯一参数启动 `chat_exporter`，从控制台列表选择要导出的联系人或群聊。

插件服务依赖微信进程，因此导出时微信需要保持运行。数据库始终以只读方式打开，但运行中
的数据库仍可能继续产生新消息；需要严格一致的历史快照时，可先在微信关闭状态复制完整
`db_storage`（包括 WAL/SHM），再重新通过 launcher 启动微信提供 key，并把复制快照的
上级目录传给导出器。

导出仍写入程序工作目录下的 `db-storage/`。`xmake run` 默认从目标输出目录运行，因此
对应目录通常是：

```text
build/windows/x64/release/db-storage/
```

直接运行 `chat_exporter.exe` 时，则使用启动它时的当前工作目录。key 只在内存中从
`http://127.0.0.1:6500/key/string` 读取，不打印、不写入 manifest 或其他文件。

## 联系人解析

程序先读取 `message_0.db.sqlite_master` 中实际存在的 `Msg_*` 表，再遍历同库 `Name2Id`，
用 `Msg_<MD5(username)>` 反查哪些用户名确实有消息表。随后用 `contact.db.contact` 补充：

- `remark`：本地备注；
- `nick_name`：联系人昵称或群名，作为显示名；
- `alias`：`nick_name` 为空时的显示名回退；
- `username`：内部唯一标识，用于区分重名联系人。

控制台使用 FTXUI 全屏界面：左侧是可滚动会话列表，右侧显示备注名、显示名、alias、
username 和会话类型。按 `/` 聚焦搜索框，可按备注、显示名、alias 或 username 实时筛选；
按 `Tab` 在搜索框和列表间切换，方向键和 `PgUp`/`PgDn` 浏览，`Enter` 导出，`Esc` 或列表
聚焦时按 `q` 退出。通讯录里没有
对应记录但确实存在消息表的会话仍会列出，此时备注和显示名可能为空，并保留 username。

群聊首先按 `username` 的 `@chatroom` 后缀判断，并用 `chat_room` 表补充确认。

## 输出目录

输入数据库位于参数指定范围内的 `db_storage/`，输出位于程序工作目录下的
`db-storage/`：

```text
<账号数据目录>/db_storage/
  contact/contact.db
  message/message_0.db
  ...

<程序工作目录>/db-storage/
  chat_export_<MD5(username)>_<YYYYMMDD_HHMMSS>/
```

导出目录名只包含 username 摘要，不泄露联系人名称。每次选择都会新建时间戳目录；若
目录已存在且非空则拒绝覆盖。

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

- 插件服务未启动、尚未捕获 key，或返回值不是 64 位十六进制字符串；
- 数据库目录不完整或 key 与快照不匹配；
- 没有任何 `Name2Id` 用户名能对应到实际 `Msg_*` 表；
- 输出目录非空；
- 微信升级导致表或压缩配置变化。

修改导出逻辑后至少验证单聊、群聊、重名联系人、压缩/非压缩正文、图片/语音/视频消息、
可选数据库缺失和空输出目录保护。所有生成物都属于私有数据。
