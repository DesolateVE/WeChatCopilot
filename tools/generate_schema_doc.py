import json
import re
import sys
from collections import defaultdict
from pathlib import Path

DATABASE_MEANINGS = {
    "bizchat/bizchat.db": "企业微信/业务聊天相关联系人、群与名称映射（名称推测）。",
    "contact/contact.db": "联系人、群聊和群成员主数据（已结合参考指南与实际表名）。",
    "contact/contact_fts.db": "联系人全文检索派生索引；部分虚拟表依赖微信私有分词器。",
    "emoticon/emoticon.db": "表情、自定义表情和表情包元数据（名称推测）。",
    "favorite/favorite.db": "收藏条目及名称映射（参考指南）。",
    "favorite/favorite_fts.db": "收藏全文检索派生索引（名称推测）。",
    "general/general.db": "撤回、转账、红包、最近联系人等通用业务状态（参考指南）。",
    "hardlink/hardlink.db": "图片、视频、文件的本地硬链接索引（参考指南）。",
    "head_image/head_image.db": "联系人或群的头像缓存索引（参考指南）。",
    "message/biz_message_0.db": "公众号及业务消息分表（参考指南）。",
    "message/media_0.db": "语音等消息媒体索引（参考指南）。",
    "message/message_0.db": "普通聊天消息主库，按会话拆分为 Msg_* 表（参考指南）。",
    "message/message_fts.db": "消息全文检索派生索引（参考指南）。",
    "message/message_resource.db": "消息与图片、文件等资源的关联索引（参考指南）。",
    "message/weclaw.db": "当前快照为空；用途待验证。",
    "session/session.db": "会话摘要、未读、草稿和删除状态（参考指南）。",
    "sns/sns.db": "朋友圈/SNS 内容及索引（名称推测）。",
    "solitaire/solitaire.db": "群接龙数据（参考指南）。",
}

KNOWN_FIELDS = {
    "id": "记录标识；具体作用域需结合所在表确认",
    "local_id": "本地消息或业务记录 ID",
    "server_id": "服务端消息 ID",
    "message_id": "消息标识",
    "username": "微信内部用户名/会话唯一标识",
    "user_name": "微信内部用户名；常用于名称映射",
    "user_name_": "微信内部用户名（尾部下划线为表内命名约定）",
    "alias": "用户设置的微信号",
    "remark": "本地备注名",
    "nick_name": "昵称或群名",
    "sort_seq": "消息排序序列",
    "real_sender_id": "真实发送者在本数据库 Name2Id 表中的 rowid",
    "create_time": "创建时间，通常为 Unix 时间戳",
    "update_time": "更新时间，通常为 Unix 时间戳",
    "modify_time": "修改时间，通常为 Unix 时间戳",
    "message_content": "消息正文；可能由 WCDB_CT_message_content 标记为 Zstd BLOB",
    "source": "消息来源/msgsource 等附加信息；可能由 WCDB_CT_source 标记为 Zstd BLOB",
    "packed_info_data": "未解析的打包扩展数据",
    "wcdb_ct_message_content": "message_content 的 WCDB 存储/压缩类型；值 4 已验证为 Zstd BLOB",
    "wcdb_ct_source": "source 的 WCDB 存储/压缩类型；值 4 已验证为 Zstd BLOB",
    "is_session": "名称映射项是否代表会话",
    "room_id": "群聊记录 ID",
    "member_id": "群成员联系人 ID",
    "chat_name_id": "会话名在当前数据库名称映射表中的 ID",
    "chat_id": "会话在当前数据库映射表中的 ID",
    "sender_id": "发送者在当前数据库映射表中的 ID",
    "session_id": "会话在当前数据库或索引中的 ID",
    "username_id": "username 在当前数据库 Name2Id 表中的 rowid",
    "unread_count": "未读数量",
    "summary": "会话最后消息摘要，不等同于完整正文",
    "draft": "会话草稿内容",
    "head_img_url": "头像图片 URL",
    "md5": "内容或标识的 MD5 摘要",
    "crc": "循环冗余校验值",
    "version": "数据结构或记录版本",
}


def field_meaning(name: str, table: str = "") -> tuple[str, str]:
    lower = name.lower()
    if lower == "local_type":
        if re.fullmatch(r"msg_[0-9a-f]{32}", table.lower()):
            return "消息类型；低 32 位通常为基础类型", "参考/已验证"
        return "当前表定义的局部记录类型；枚举含义待验证", "名称推测"
    if lower in KNOWN_FIELDS:
        basis = "参考/已验证" if lower in {
            "username", "alias", "remark", "nick_name", "local_id", "server_id",
            "sort_seq", "real_sender_id", "create_time", "message_content",
            "source", "packed_info_data", "wcdb_ct_message_content", "wcdb_ct_source",
            "is_session", "room_id", "member_id", "summary"
        } else "名称推测"
        return KNOWN_FIELDS[lower], basis
    if lower.startswith("wcdb_ct_"):
        return f"{name[8:]} 字段的 WCDB 存储/压缩类型标记", "名称推测"
    if lower.startswith("reserved") or lower.startswith("reserve"):
        return "保留字段，实际用途待样本或代码验证", "待验证"
    rules = [
        (r"(^|_)rowid$", "SQLite 行标识或关联行 ID"),
        (r"_id$", "关联记录 ID；仅在当前数据库的 ID 空间内有效"),
        (r"(_time|timestamp|_ts)$", "时间值；单位和时区需结合样本验证"),
        (r"(_count|count_)|^count$", "数量或计数"),
        (r"(_flag|flags|bit_flag|_mask)$", "位标志集合"),
        (r"(^is_|^has_|^enable|_enabled$)", "布尔或状态标记"),
        (r"(_type|^type$)", "类型枚举值"),
        (r"(_status|^status$|_state|^state$)", "状态枚举值"),
        (r"(_url|url_)", "网络资源地址"),
        (r"(_path|path_)", "本地或逻辑路径"),
        (r"(_name|name_)", "名称或显示名称"),
        (r"(_data|data_|_blob|blob_)", "二进制或序列化数据，编码需进一步确认"),
        (r"(_content|content_)", "正文或内容数据"),
        (r"(_seq|sequence)", "排序或递增序列"),
        (r"(_size|length|_len)$", "数据长度或尺寸"),
        (r"(_key|key_)", "键值、索引键或业务键"),
    ]
    for pattern, meaning in rules:
        if re.search(pattern, lower):
            return meaning, "名称推测"
    return "用途未知，需结合样本值、调用代码或逆向结果验证", "待验证"


def table_meaning(database: str, table: str) -> str:
    lower = table.lower()
    if re.fullmatch(r"msg_[0-9a-f]{32}", lower):
        return "某一会话的消息分表；后缀通常为会话 username 的 MD5。"
    if lower in {"name2id", "chatname2id", "sendername2id", "dir2id"}:
        return "名称到当前数据库内部 rowid 的映射表；不可直接跨库连接。"
    if lower == "contact":
        return "联系人主表，包含内部 username、微信号、备注和昵称等。"
    if lower in {"chat_room", "chatroom_member"}:
        return "群聊或群成员关系表。"
    if lower == "sessiontable":
        return "会话摘要与排序状态主表，不保存完整消息正文。"
    if "fts" in lower:
        if lower.endswith(("_content", "_segments", "_segdir", "_docsize", "_stat", "_data", "_idx", "_config")):
            return "全文检索虚拟表的影子/内部表，不应视为独立业务实体。"
        return "全文检索相关虚拟表或范围索引；可能依赖微信私有分词器。"
    if "hardlink" in lower:
        return "本地媒体或文件硬链接索引。"
    if "resource" in lower:
        return "消息资源或资源关联数据。"
    if "unread" in lower:
        return "会话未读状态或统计。"
    if "draft" in lower:
        return "会话草稿数据。"
    if "delete" in lower:
        return "删除记录或删除状态。"
    if "revoke" in lower:
        return "消息撤回记录。"
    if "transfer" in lower:
        return "转账相关记录。"
    if "redenvelope" in lower:
        return "红包相关记录。"
    if "solitaire" in lower:
        return "群接龙相关数据。"
    return f"位于 {database} 的业务或内部表；用途主要依据表名推测。"


def esc(value) -> str:
    if value is None:
        return "NULL"
    return str(value).replace("|", "\\|").replace("\n", " ")


def signature(table: dict) -> tuple:
    return tuple((c.get("name"), c.get("type"), c.get("notNull"), c.get("defaultValue"), c.get("primaryKey")) for c in table.get("columns", []))


def compact_table_names(names: list[str], separator: str = ", ") -> str:
    message_table_count = sum(bool(re.fullmatch(r"Msg_[0-9a-f]{32}", name)) for name in names)
    compacted = []
    message_tables_added = False
    for name in names:
        if re.fullmatch(r"Msg_[0-9a-f]{32}", name):
            if not message_tables_added:
                compacted.append(f"Msg_<md5>[count={message_table_count}]")
                message_tables_added = True
        else:
            compacted.append(name)
    return separator.join(compacted)


def generate_ai_context(schema: dict) -> str:
    databases = schema.get("databases", [])
    table_count = sum(len(db.get("tables", [])) for db in databases)
    column_count = sum(len(table.get("columns", [])) for db in databases for table in db.get("tables", []))
    out = [
        "# WeChat DB AI Context",
        "",
        "PURPOSE: compact retrieval context for AI; not a developer tutorial.",
        "SOURCE: current snapshot sqlite_master + PRAGMA table_info. Meanings use external guide only as non-authoritative evidence.",
        "CONFIDENCE: V=sample/reference verified; N=name inference; U=unknown. Never present N/U as fact.",
        "",
        "## HARD_CONSTRAINTS",
        "",
        "- Numeric IDs/rowid are database-local. NEVER join numeric IDs across databases.",
        "- Cross-DB joins must resolve to username/user_name first.",
        "- SessionTable.summary is a summary, not complete message content.",
        "- Msg_<md5> is a per-conversation table; observed mapping is MD5(username UTF-8), but revalidate after WeChat upgrades.",
        "- WCDB_CT_message_content/source=4 marks corresponding Zstd BLOB in verified samples.",
        "- Any BLOB may be probed as Zstd; failed probe means preserve/render as hex.",
        "- FTS virtual tables may fail without MMFtsTokenizer; use shadow *_content tables when appropriate.",
        "- Schema proves shape only. It does not prove semantic relationships or enum meanings.",
        "",
        "## CORE_RELATIONS",
        "",
        "```text",
        "contact.contact(username|alias|remark|nick_name) -> username",
        "username -> message_0.Name2Id.user_name -> rowid/is_session",
        "username -> table Msg_ + MD5(username UTF-8)",
        "Msg_*.real_sender_id -> SAME_DB Name2Id.rowid -> Name2Id.user_name",
        "contact.chat_room.id -> contact.chatroom_member.room_id",
        "contact.chatroom_member.member_id -> contact.contact.id",
        "session.SessionUnreadListTable_1.username_id -> session.Name2Id.rowid",
        "message_resource.MessageResourceInfo.chat_id -> SAME_DB ChatName2Id.rowid",
        "message_resource.MessageResourceInfo.sender_id -> SAME_DB SenderName2Id.rowid",
        "message_resource.MessageResourceDetail.message_id -> MessageResourceInfo.message_id",
        "media.VoiceInfo.chat_name_id -> media.Name2Id.rowid",
        "hardlink.*_hardlink_info_v4.dir2 -> hardlink.dir2id.rowid",
        "favorite.fav_db_item.fromusr_id/realchatname_id -> favorite.Name2Id.rowid",
        "```",
        "",
        "## RETRIEVAL",
        "",
        "- Exact machine schema: `docs/ai/database_schema.json`.",
        "- APIs: `/api/databases`; `/api/tables?database=...`; `/api/schema?database=...`; `/api/schema/:table?database=...`; `/api/tables/:table?database=...&page=1&pageSize=20`.",
        "- Compact column token: `cid:name:type:NN:PKn:default => meaning[confidence]`; `-` means absent/false/null.",
        "",
        f"## SNAPSHOT databases={len(databases)} tables={table_count} column_defs={column_count} root={esc(schema.get('root', ''))}",
        "",
    ]
    for db in databases:
        database = db.get("database", "")
        tables = db.get("tables", [])
        out.extend([f"## DB `{database}` tables={len(tables)}", f"ROLE: {DATABASE_MEANINGS.get(database, '用途待验证。')}"])
        if db.get("error"):
            out.append(f"ERROR: {db['error']}")
        if not tables:
            out.extend(["TABLES: -", ""])
            continue
        out.append("TABLES: " + compact_table_names([t.get("name", "") for t in tables]))
        groups = defaultdict(list)
        representatives = {}
        for table in tables:
            key = signature(table)
            groups[key].append(table.get("name", ""))
            representatives[key] = table
        for group_index, (key, names) in enumerate(groups.items(), 1):
            table = representatives[key]
            out.append(f"G{group_index} TABLES=" + compact_table_names(names, ","))
            out.append("ROLE=" + table_meaning(database, names[0]))
            tokens = []
            for column in table.get("columns", []):
                meaning, basis = field_meaning(column.get("name", ""), names[0])
                confidence = {"参考/已验证": "V", "名称推测": "N", "待验证": "U"}[basis]
                tokens.append(
                    f"{column.get('cid')}:{esc(column.get('name'))}:{esc(column.get('type')) or '-'}:"
                    f"{'NN' if column.get('notNull') else '-'}:"
                    f"{'PK' + str(column.get('primaryKey')) if column.get('primaryKey') else '-'}:"
                    f"{esc(column.get('defaultValue'))} => {esc(meaning)}[{confidence}]"
                )
            out.append("COLS=" + "; ".join(tokens))
            if table.get("error"):
                out.append(f"ERROR={table['error']}")
        out.append("")
    return "\n".join(out)


def generate_developer_guide(schema: dict) -> str:
    databases = schema.get("databases", [])
    table_count = sum(len(db.get("tables", [])) for db in databases)
    return f"""# 微信数据库开发者指南

> 基于当前快照的实际结构总结，只讲值得关注的入口、关系和限制。字段语义包含样本验证与合理推测，不是微信官方规范。

## 总览

当前快照包含 {len(databases)} 个数据库、{table_count} 张表。日常开发主要关注以下数据链：

```text
联系人输入
  -> contact/contact.db: contact.username
  -> message/message_0.db: Name2Id.user_name
  -> Msg_<MD5(username UTF-8)>
  -> 消息正文、真实发送者、资源和媒体
```

最重要的原则：**数字 ID 和 rowid 只在所属数据库内有效。跨库关联必须先转换成 `username`/`user_name`。**

## 核心数据库

| 数据库 | 关注点 |
|---|---|
| `contact/contact.db` | 联系人、群聊、群成员；身份解析入口 |
| `message/message_0.db` | 普通聊天消息，按会话拆成 `Msg_*` 表 |
| `message/biz_message_0.db` | 公众号和业务消息，结构与普通消息分表相近 |
| `session/session.db` | 会话摘要、未读和草稿；不是完整正文 |
| `message/message_resource.db` | 图片、文件等资源与消息的关联 |
| `message/media_0.db` | 语音数据和会话映射 |
| `hardlink/hardlink.db` | 本地图片、视频、文件路径索引 |
| `general/general.db` | 撤回、转账、红包等业务状态 |
| `message/message_fts.db` | 全文检索派生数据；虚拟表可能依赖私有分词器 |

## 联系人与群

`contact` 是身份解析入口，优先使用：

1. `username`：跨库稳定标识。
2. `alias`：用户设置的微信号。
3. `remark`：本地备注。
4. `nick_name`：昵称或群名，可能重名。

群关系：

```text
chat_room.id -> chatroom_member.room_id
chatroom_member.member_id -> contact.id
```

群聊通常可通过 `username` 以 `@chatroom` 结尾识别。`chat_room.ext_buffer`、`contact.extra_buffer` 等 BLOB 的内部格式尚未确认。

## 消息分表

`message_0.db.Name2Id` 保存 `user_name` 与本库 rowid。观测到 `is_session != 0` 的会话通常对应：

```text
Msg_<MD5(username UTF-8)>
```

消息表关键字段：

| 字段 | 用途 |
|---|---|
| `local_id` / `server_id` | 本地/服务端消息 ID |
| `local_type` | 消息类型；低 32 位通常为基础类型 |
| `sort_seq` | 时间线排序首选字段 |
| `real_sender_id` | 发送者在同库 `Name2Id` 中的 rowid |
| `create_time` | Unix 秒时间戳 |
| `message_content` | 文本、XML 或其他正文 |
| `source` | `msgsource` 等附加信息 |
| `packed_info_data` | 未解析扩展 BLOB |
| `WCDB_CT_message_content` | 正文存储/压缩标记 |
| `WCDB_CT_source` | source 存储/压缩标记 |

真实发送者必须按同库映射：

```text
Msg_*.real_sender_id -> message_0.Name2Id.rowid -> Name2Id.user_name
```

不要依赖正文开头的 `wxid_...` 文本判断发送者。

## 压缩与 BLOB

当前样本中，`WCDB_CT_message_content == 4` 和 `WCDB_CT_source == 4` 表示对应值为 Zstd BLOB。浏览器实现会对所有 BLOB 尝试 Zstd：

- 解压成功：显示正文并使用琥珀色标记。
- 解压失败：保留内容并以空格分隔的大写十六进制显示，使用蓝色标记。
- 解压上限：512 MB。

通用 BLOB 探测是便利功能，不代表每个 BLOB 都应该是 Zstd。

## 会话、资源与媒体

`SessionTable.summary` 只是最后消息摘要；完整正文必须读取消息分表。

```text
SessionUnreadListTable_1.username_id -> session.Name2Id.rowid
MessageResourceInfo.chat_id -> message_resource.ChatName2Id.rowid
MessageResourceInfo.sender_id -> message_resource.SenderName2Id.rowid
MessageResourceDetail.message_id -> MessageResourceInfo.message_id
VoiceInfo.chat_name_id -> media_0.Name2Id.rowid
*_hardlink_info_v4.dir2 -> hardlink.dir2id.rowid
```

资源表与消息表常可通过本地消息 ID、服务端消息 ID、会话 username 和创建时间联合核对；不要假设单个数字 ID 可跨库直连。

## FTS 限制

部分 FTS 虚拟表需要微信私有 `MMFtsTokenizer`，直接查询会出现 `no such tokenizer`。这不是数据库损坏。需要读取派生索引时，优先检查标准 SQLite 可读的 `*_content` 等影子表；完整聊天正文仍以 `message_0.db` 为准。

## 只读接口

- `GET /api/databases`
- `GET /api/tables?database=<相对路径>`
- `GET /api/schema?database=<相对路径>`
- `GET /api/schema/:table?database=<相对路径>`
- `GET /api/tables/:table?database=<相对路径>&page=1&pageSize=20`

离线导出全部结构：

```powershell
wechat_copilot.exe --schema-json docs/ai/database_schema.json
```

完整机器结构见 `docs/ai/database_schema.json`；面向 AI 的紧凑结构索引见 `docs/ai/DATABASE_SCHEMA.md`。

## 版本风险

- 微信升级后表名、字段和压缩方式都可能变化。
- 名称推测不能替代样本验证或调用代码分析。
- 复制数据库时应同时处理 WAL/SHM，并尽量在微信关闭后获取一致快照。
"""


def main() -> int:
    source = Path(sys.argv[1] if len(sys.argv) > 1 else "docs/ai/database_schema.json")
    target = Path(sys.argv[2] if len(sys.argv) > 2 else "docs/ai/DATABASE_SCHEMA.md")
    developer_target = Path(sys.argv[3] if len(sys.argv) > 3 else "docs/developer/DATABASE_GUIDE.md")
    schema = json.loads(source.read_text(encoding="utf-8"))
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(generate_ai_context(schema), encoding="utf-8", newline="\n")
    developer_target.parent.mkdir(parents=True, exist_ok=True)
    developer_target.write_text(generate_developer_guide(schema), encoding="utf-8", newline="\n")
    print(f"Wrote {target}")
    print(f"Wrote {developer_target}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
