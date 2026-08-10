# WeChatCopilot

Windows 微信 4.x 数据库只读浏览、结构分析与逆向研究项目。仓库把可运行产品、
人类文档、AI 上下文和包含隐私的本机数据明确分开，避免研究记录与业务代码相互污染。

## 目录结构

```text
src/db_explorer/         数据库浏览服务
src/chat_exporter/       按联系人完整导出聊天信息的 WCDB 命令行工具
web/                     浏览器前端
tools/                   当前项目的生成与维护脚本
docs/developer/          面向开发者的指南、证据与操作手册
docs/ai/                 面向 AI 的恢复上下文和机器结构索引
research/weixin-db/      从 WeChatAIPlugin 迁入的独立逆向工具链
local-data/              本机数据库、密钥和导出物（禁止提交）
```

文档入口见 [docs/README.md](docs/README.md)。逆向工具链的构建与使用见
[research/weixin-db/README.md](research/weixin-db/README.md)。

## 构建当前应用

项目使用 xmake、C++20、WCDB、cpp-httplib 和 nlohmann-json：

```powershell
git submodule update --init --recursive
cmake -S .deps/wcdb/src -B .deps/wcdb/src/build -A x64 -DWCDB_CPP=ON -DWCDB_ZSTD=ON
cmake --build .deps/wcdb/src/build --config Release --parallel
xmake f -m release
xmake
```

首次克隆也可使用 `git clone --recurse-submodules`。仓库只记录固定的 WCDB commit；
WCDB、SQLCipher 和 Zstd 源码及其本机构建产物不会复制到本仓库。

运行前通过环境变量提供本机数据，不在源码中保存密钥：

```powershell
$env:WECHAT_DB_KEY_HEX = '<本机 32 字节 WCDB key 的十六进制形式>'
$env:WECHAT_DB_DIR = (Resolve-Path '.\local-data\db-storage').Path
xmake run db_explorer
```

默认监听 `http://127.0.0.1:8090`。数据库访问保持只读；采集快照时应先关闭微信，
并同时保留数据库的 WAL/SHM 文件。

## 导出联系人聊天

`chat_exporter` 可用 `username`、`alias`、`remark` 或 `nick_name` 查询。备注或昵称
重名时会显示候选项并要求选择：

```powershell
xmake run chat_exporter -- '<查询值>'
```

工具导出消息、发送者、会话状态、资源、语音、FTS 派生数据、群成员及其他关联记录，
默认写入 `local-data/exports/`。详细用法见
[聊天导出器文档](docs/developer/CHAT_EXPORTER.md)。

## 数据边界

`local-data/` 中的数据库、聊天导出和密钥均为私有本机数据。它们不会作为开发文档或
AI 上下文的一部分。AI 所需的结构信息只使用 `docs/ai/` 下脱敏后的 schema 与结论。
