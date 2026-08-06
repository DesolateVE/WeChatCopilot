# WeChatCopilot

Windows 微信 4.x 数据库只读浏览、结构分析与逆向研究项目。仓库把可运行产品、
人类文档、AI 上下文和包含隐私的本机数据明确分开，避免研究记录与业务代码相互污染。

## 目录结构

```text
src/                     数据库浏览服务与命令行入口
web/                     浏览器前端
tools/                   当前项目的生成与维护脚本
docs/developer/          面向开发者的指南、证据与操作手册
docs/ai/                 面向 AI 的恢复上下文和机器结构索引
docs/reference/          原始参考文档
research/weixin-db/      从 WeChatAIPlugin 迁入的独立逆向工具链
local-data/              本机数据库、密钥和导出物（禁止提交）
```

文档入口见 [docs/README.md](docs/README.md)。逆向工具链的构建与使用见
[research/weixin-db/README.md](research/weixin-db/README.md)。

## 构建当前应用

项目使用 xmake、C++20、WCDB、cpp-httplib 和 nlohmann-json：

```powershell
xmake f -m release
xmake
```

运行前通过环境变量提供本机数据，不在源码中保存密钥：

```powershell
$env:WECHAT_DB_KEY_HEX = '<本机 32 字节 WCDB key 的十六进制形式>'
$env:WECHAT_DB_DIR = (Resolve-Path '.\local-data\db-storage').Path
xmake run wechat_copilot
```

默认监听 `http://127.0.0.1:8090`。数据库访问保持只读；采集快照时应先关闭微信，
并同时保留数据库的 WAL/SHM 文件。

## 数据边界

`local-data/` 中的数据库、聊天导出和密钥均为私有本机数据。它们不会作为开发文档或
AI 上下文的一部分。AI 所需的结构信息只使用 `docs/ai/` 下脱敏后的 schema 与结论。

