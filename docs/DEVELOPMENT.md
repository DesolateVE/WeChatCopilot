# 开发指南

本文描述当前仓库的代码与运行方式。数据库语义见 [DATABASES.md](DATABASES.md)，
版本相关逆向证据见 [REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md)。

## 目录与目标

```text
src/db_explorer/    WCDB 只读访问、HTTP 服务
src/chat_exporter/  会话解析与 JSONL 导出
src/chat_launcher/  使用 Detours 启动并注入微信
src/chat_plugin/    WCDB key 捕获 DLL
web/                db_explorer 的单页前端
tools/              Schema 文档生成和 Weixin.dll 离线定位工具
docs/ai/            自动生成的 Schema 索引和机器 JSON
local-data/         本机私有数据；除 README 外均忽略
```

四个 xmake 目标：

| 目标 | 类型 | 主要依赖 |
|---|---|---|
| `db_explorer` | EXE | WCDB、SQLCipher、Zstd、cpp-httplib、nlohmann-json |
| `chat_exporter` | EXE | WCDB、SQLCipher、Zstd、nlohmann-json |
| `chat_launcher` | EXE | Microsoft Detours |
| `chat_plugin` | DLL | Microsoft Detours、cpp-httplib |

## 构建

```powershell
git submodule update --init --recursive
cmake -S .deps/wcdb/src -B .deps/wcdb/src/build -A x64 -DWCDB_CPP=ON -DWCDB_ZSTD=ON
cmake --build .deps/wcdb/src/build --config Release --parallel
xmake f -m release
xmake
```

单独构建：

```powershell
xmake build db_explorer
xmake build chat_exporter
xmake build chat_launcher
xmake build chat_plugin
```

`db_explorer` 和 `chat_exporter` 构建后会把 `WCDB.dll` 复制到目标目录；
`db_explorer` 还会复制 `web/index.html`。`chat_launcher.exe` 要求
`chat_plugin.dll` 位于同一目录。

## 数据库浏览器

### 运行

```powershell
$env:WECHAT_DB_KEY_HEX = '<64 位十六进制 WCDB key>'
$env:WECHAT_DB_DIR = (Resolve-Path '.\local-data\db-storage').Path
xmake run db_explorer
```

`WECHAT_DB_KEY_HEX` 必填；`WECHAT_DB_DIR` 默认是 `local-data/db-storage`。服务只监听
`127.0.0.1:8090`，页面数据不会主动发送到外部网络。

### HTTP API

| 请求 | 作用 |
|---|---|
| `GET /api/databases` | 列出根目录下的 `.db` 文件 |
| `GET /api/tables?database=<path>` | 列出数据库中的表 |
| `GET /api/schema?database=<path>` | 返回整库 Schema |
| `GET /api/schema/:table?database=<path>` | 返回单表 Schema |
| `GET /api/tables/:table?database=<path>&page=1&pageSize=20` | 分页读取表数据 |

`pageSize` 被限制在 1–100。数据库路径必须位于配置的根目录中且以 `.db` 结尾，避免
通过 API 访问根目录之外的文件。BLOB 会先尝试 Zstd 解压；失败时以空格分隔的十六进制
显示，单值解压上限为 512 MiB。

### 更新 Schema 索引

在明确允许读取本机快照时执行：

```powershell
xmake run db_explorer -- --schema-json docs/ai/database_schema.json
python tools/generate_schema_doc.py
```

第一条命令更新精确机器 Schema，第二条命令更新紧凑的
`docs/ai/DATABASE_SCHEMA.md`。生成内容只描述结构，不应包含真实行数据。

## 聊天导出器

```powershell
xmake run chat_exporter -- 'C:\path\to\xwechat_files\wxid_xxx_0000'
```

它只接收账号数据根目录（也可直接传 `db_storage`），在其下自动定位包含
`contact/contact.db` 和 `message/message_0.db` 的 `db_storage`。程序从
`http://127.0.0.1:6500/key/string` 获取 key，使用工作目录下的 `db-storage/` 作为导出
根目录。控制台通过 FTXUI 提供搜索、滚动列表和联系人详情，只列出存在 `Msg_*` 表的会话；
选择后输出单个会话的 JSONL。完整流程见
[CHAT_EXPORTER.md](CHAT_EXPORTER.md)。

## 密钥捕获组件

`chat_launcher` 使用 `DetourCreateProcessWithDllsW` 启动微信并注入同目录的
`chat_plugin.dll`：

```powershell
xmake run chat_launcher -- 'C:\path\to\Weixin.exe'
```

插件当前行为：

1. Hook `LoadLibraryExW`，等待 `Weixin.dll` 完成加载。
2. 读取已加载模块的文件版本，并从版本表选择 `SetCipherKey` RVA。
3. 校验目标函数入口签名；未知版本或签名不符时拒绝安装 Hook。
4. 保存首次观察到的 WCDB key 字节，然后移除该 Hook。
5. 仅在 `127.0.0.1:6500` 提供：
   - `GET /key/string`：64 位十六进制字符串；
   - `GET /key/bytes`：32 字节二进制数据。

当前版本表：

| 微信版本 | `SetCipherKey` RVA | 证据状态 |
|---|---:|---|
| `4.1.12.26` | `0x5DBF40` | 静态定位 + 动态 key/数据库验证 |
| `4.1.13.12` | `0x5ECD00` | 静态锚点、内部语义片段和 `.pdata` 函数边界验证 |

`4.1.13.12` 尚未在本文中提升为动态确认；`WCDB_Data` 布局、页大小和 compatibility
沿用旧版结论，实际使用前仍应按[逆向参考](REVERSE_ENGINEERING.md#wcdb-密钥边界)观察
参数并用只读数据库交叉验证。插件也不会区分首次 key 属于哪个数据库；首次打开顺序变化
时仍需确认。key 会保留在目标进程内存中，HTTP 响应也属于敏感数据。

升级微信后可先运行无第三方依赖的离线定位器：

```powershell
python tools/locate_weixin_set_cipher_key.py 'C:\path\to\Weixin.dll'
```

只有入口、配置对象构造、配置安装、配置移除四个模式均为单一命中，落在同一 `.pdata`
函数边界，且 Cipher 配置名与全局配置键交叉引用一致时，脚本才输出候选 RVA。该结果仍
不能替代动态参数和数据库验证。

## 修改与验证

最低验证集合：

```powershell
xmake build db_explorer
xmake build chat_exporter
xmake build chat_launcher
xmake build chat_plugin
python tools/generate_schema_doc.py
```

涉及数据库访问时，再用脱敏或私有快照验证：

- 所有打开路径保持只读；
- 错误 key 能明确失败；
- 数据库只从传入目录下定位出的 `db_storage/` 读取，导出只写入工作目录的 `db-storage/`；
- 消息正文和 `source` 的压缩/非压缩分支都能读取；
- 单聊、群聊和重名联系人都覆盖；
- 输出目录拒绝覆盖已有内容；
- 文档与程序的参数、端口、目标名一致。

## 维护约束

- 微信版本变化后重新验证 RVA、对象布局、页大小、兼容级别、表结构和压缩标记。
- 新增插件版本时同步更新 `src/chat_plugin/versions.hpp` 和逆向证据，不能只改硬编码 RVA。
- 跨库不得直接连接数字 ID；先转换为 `username` 或 `user_name`。
- `docs/ai/DATABASE_SCHEMA.md` 是生成文件，不手工编辑；修改生成逻辑后重新运行脚本。
- 不把真实 key、账号标识、聊天正文、数据库行或调试转储写入源码、文档、测试或提交。
