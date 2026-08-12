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
tools/              Schema 文档生成工具
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
xmake run chat_exporter -- '<查询值>'
```

它和浏览器共享 `WECHAT_DB_KEY_HEX`、`WECHAT_DB_DIR`，但面向单个会话输出 JSONL。
完整接口见 [CHAT_EXPORTER.md](CHAT_EXPORTER.md)。

## 密钥捕获组件

`chat_launcher` 使用 `DetourCreateProcessWithDllsW` 启动微信并注入同目录的
`chat_plugin.dll`：

```powershell
xmake run chat_launcher -- 'C:\path\to\Weixin.exe'
```

插件当前行为：

1. Hook `LoadLibraryExW`，等待 `Weixin.dll` 完成加载。
2. 在 `Weixin.dll + 0x5DBF40` 安装 `SetCipherKey` Hook。
3. 保存首次观察到的 WCDB key 字节，然后移除该 Hook。
4. 仅在 `127.0.0.1:6500` 提供：
   - `GET /key/string`：64 位十六进制字符串；
   - `GET /key/bytes`：32 字节二进制数据。

该 RVA 和 `WCDB_Data` 布局仅在微信 `4.1.12.26` 上确认。当前实现没有在注入前自动验证
微信版本，也不会区分首次 key 属于哪个数据库；升级后或首次打开顺序变化时必须先按
[逆向参考](REVERSE_ENGINEERING.md#wcdb-密钥边界) 重新验证。key 会保留在目标进程内存中，
HTTP 响应也属于敏感数据。

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
- 路径不能逃逸 `WECHAT_DB_DIR`；
- 消息正文和 `source` 的压缩/非压缩分支都能读取；
- 单聊、群聊和重名联系人都覆盖；
- 输出目录拒绝覆盖已有内容；
- 文档与程序的参数、端口、目标名一致。

## 维护约束

- 微信版本变化后重新验证 RVA、对象布局、页大小、兼容级别、表结构和压缩标记。
- 跨库不得直接连接数字 ID；先转换为 `username` 或 `user_name`。
- `docs/ai/DATABASE_SCHEMA.md` 是生成文件，不手工编辑；修改生成逻辑后重新运行脚本。
- 不把真实 key、账号标识、聊天正文、数据库行或调试转储写入源码、文档、测试或提交。
