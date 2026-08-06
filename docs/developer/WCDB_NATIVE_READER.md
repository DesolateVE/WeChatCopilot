# WCDB 原生读取与 IDA 参考库

## 已验证结论

这条路线可行，而且比直接操作 SQLCipher 更贴近微信自身的数据层。

当前工程固定使用腾讯 WCDB `v2.1.16`：

```text
commit: df808591b9f9a9ab42156006819c3550d5af13a3
target: Windows x64 / MSVC / Release / static WCDB / static CRT
features: C++ + SQLCipher + Zstd
```

已生成的主要构建产物位于：

```text
.deps/wcdb/build-static-mt-x64/Release/WCDB.lib
.deps/wcdb/build-static-mt-x64/Release/WCDB.pdb
.deps/wcdb/build-static-mt-x64/Release/sqlcipher.lib
.deps/wcdb/build-static-mt-x64/Release/zstd.lib
```

依赖源码、构建目录、密钥和聊天快照均已被 `.gitignore` 排除。

## 重新拉取并编译 WCDB

环境要求：

- Git
- CMake
- Visual Studio 2026 C++ x64 工具链

执行：

```powershell
.\scripts\build_wcdb_static.ps1
```

脚本会浅克隆官方仓库和子模块，并校验固定 commit；如果现有 checkout
不是预期版本，脚本会停止而不会覆盖它。

官方参考：

- <https://github.com/Tencent/wcdb>
- <https://github.com/Tencent/wcdb/wiki/C%2B%2B-%E5%AE%89%E8%A3%85%E4%B8%8E%E5%85%BC%E5%AE%B9%E6%80%A7>
- <https://github.com/Tencent/wcdb/wiki/C%2B%2B-%E5%8A%A0%E5%AF%86%E4%B8%8E%E9%85%8D%E7%BD%AE>
- <https://github.com/Tencent/wcdb/wiki/C---%E6%95%B0%E6%8D%AE%E5%8E%8B%E7%BC%A9>

## 编译并运行原生读取器

```powershell
cmake -S . -B build\wcdb-reader-mt -G "Visual Studio 18 2026" -A x64
cmake --build build\wcdb-reader-mt --config Release --parallel 12

.\build\wcdb-reader-mt\Release\weixin_wcdb_reader.exe `
  .\artifacts\exports\offline_message_0_snapshot_20260730\message_0.db `
  .\artifacts\exports\wcdb_cipher_key_message_0.json
```

读取器的关键流程是：

```text
WCDB::Database(path, readOnly=true)
  -> setCipherKey(captured_key, pageSize=4096, Version4)
  -> setCompression(message_content/source = normal Zstd)
  -> canOpen()
  -> WINQ 查询 Msg_* 表
```

必须在任何数据库操作前调用 `setCipherKey()` 和 `setCompression()`。WCDB 会
对 WINQ 查询自动改写，因此 `WCDB_CT_message_content = 4` 的 BLOB 会直接以
解压后的 Text 返回。读取器不会打印密钥或消息正文，并会在退出时清零自己的
密钥缓冲区。

本机快照的实测结果：

```json
{
  "opened": true,
  "read_only": true,
  "cipher_page_size": 4096,
  "cipher_version": 4,
  "message_tables": 148,
  "message_rows": 180606,
  "compressed_sample_found": true,
  "compressed_sample_returned_as_text": true,
  "compressed_sample_utf8_bytes": 115
}
```

表数、消息数与现有 Python/SQLCipher 离线读取结果完全一致；透明 Zstd 解压
也已由真实压缩记录验证。

## SQLite 结构摘要

SQLite 和 MySQL 都采用表、字段、记录、索引等关系模型。SQLite 通常把一个
数据库放在单个文件中，不需要独立数据库服务器；字段类型采用更灵活的类型
亲和规则。

使用 `--schema-summary` 可遍历 `sqlite_master`、每张表的
`PRAGMA table_info` 和记录数，同时只输出分组统计与少量代表表：

```powershell
.\build\wcdb-reader-mt\Release\weixin_wcdb_reader.exe `
  .\artifacts\exports\offline_message_0_snapshot_20260730\message_0.db `
  .\artifacts\exports\wcdb_cipher_key_message_0.json `
  --schema-summary
```

该模式不会读取或打印聊天正文。它输出：

- table、index、view、trigger 的数量；
- application、message shards、WCDB internal、SQLite internal 分组；
- 每组表数、字段数、记录数和单表字段范围；
- 最大的少量非消息表；
- `Name2Id` 和一张 `Msg_*` 表的代表字段，最多展示 12 个字段。

## 导出普通 SQLite / 使用 Navicat

`tools/offline/export_plain_sqlite.py` 使用已经捕获并保存在本机的密钥，只读
打开冻结快照，再通过 SQLCipher 的 `sqlcipher_export()` 生成无加密的标准
SQLite 3 文件：

```powershell
$env:PYTHONPATH=(Resolve-Path '.\.tools\python').Path
python .\tools\offline\export_plain_sqlite.py `
  .\artifacts\exports\offline_message_0_snapshot_20260730\message_0.db `
  .\artifacts\exports\message_0_plain.sqlite
```

脚本不会覆盖现有输出。它先写入 `.partial` 临时文件，并在普通 Python
`sqlite3` 驱动的文件头、`PRAGMA quick_check`、对象数和消息记录数校验全部
通过后才改成最终文件名。

在 Navicat 中新建 **SQLite** 连接，并选择 **Existing Database File**：

```text
Database File: artifacts/exports/message_0_plain.sqlite
User Name:     留空
Password:      留空
Encryption:    None / 关闭
```

SQLite 是嵌入式文件数据库，不使用 MySQL 那样的数据库用户名和服务器登录
密码。这个副本已经去除页级加密，因此也不需要填写此前捕获的 WCDB 密钥。
建议在 Navicat 中以只读方式浏览，避免把可视化工具写入的配置或索引混入
分析副本。

导出保留原始表、字段、索引和记录。微信通过 WCDB 在 SQLite 层之上实现的
列压缩也被原样保留，所以部分 `message_content` 和 `source` 在 Navicat 中
仍显示为 BLOB；对应的 `WCDB_CT_* = 4` 表示该值需要按 WCDB/Zstd 规则解压。
这不影响查看数据库结构。如果需要直接阅读这些字段，应使用原生 WCDB
读取器，或另外生成一份经过字段转换的浏览副本。

## VS Code / clangd

全局 clangd 配置使用 `--compile-commands-dir=.vscode`。运行以下脚本可用
Ninja/MSVC 执行真实构建，并把 CMake compilation database 同步到该目录：

```powershell
.\scripts\generate_compile_commands.ps1
```

生成的 `.vscode/compile_commands.json` 包含 WCDB export headers、MSVC、
Windows SDK、C++17 和 `/MT` 参数。文件中含本机绝对路径，因此已被
`.gitignore` 排除。

## 给 IDA 生成 FLIRT 签名

IDA Pro 客户提供的 FLAIR 工具可从 MSVC COFF 静态库生成 FLIRT 签名：

```powershell
.\tools\ida\build_wcdb_flirt.ps1 `
  -FlairDirectory E:\ida93sp2\tools\flair `
  -IdaDirectory E:\ida93sp2
```

脚本依次调用：

```text
pcf.exe WCDB.lib wcdb-2.1.16-msvc-x64.pat
sigmake.exe -r -n... wcdb-2.1.16-msvc-x64.pat wcdb-2.1.16-msvc-x64.sig
```

若 `sigmake` 产生 `.exc`，表示存在冲突模式，需要按 FLAIR 规则人工选择或
排除冲突名称后重跑。成功后在 IDA 使用：

```text
File -> Load file -> FLIRT signature file
```

加载签名并等待自动分析结束后，可在 IDA 执行
`tools/ida/report_wcdb_flirt_matches.py`，统计被标记为库函数以及包含 WCDB 名称的
函数。脚本先调用 `ida_auto.auto_wait()`，不依赖任何硬编码地址。

已使用 IDA 9.3 SP2 FLAIR 成功生成并校验：

```text
signature: WCDB 2.1.16 MSVC x64
modules:   7806
format:    version 10 / 32-byte patterns / Intel 80x86
SHA-256:   021E3B53E4EDA4EB3ED5D67894F9824A5ACC4DAA9BB70C4D1BA0D17D2A9C7C01
```

签名保存在 `artifacts/ida/wcdb-2.1.16-msvc-x64.sig`。传入
`-IdaDirectory` 时，脚本也会将其安装到 IDA 的 `sig/pc` 目录；如果目标
存在但哈希不同，脚本会拒绝覆盖。

其他可用参考：

- IDA 可直接打开 `WCDB.lib` 中的 COFF object；
- 可打开原生读取器 EXE 并加载其 PDB 做并排比较；
- `tools/ida/export_wcdb_symbols.ps1` 可导出 demangle 后的外部符号清单。

FLIRT 是编译器、架构和编译选项相关的。当前库使用 VS 2026/MSVC 19.51；
如果微信使用了不同 MSVC 版本、LTO 或源码分支，签名只会命中相同或近似的
函数子集，不应把未命中理解为微信没有使用 WCDB。
