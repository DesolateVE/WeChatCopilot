# WeChat DB AI Context

PURPOSE: compact retrieval context for AI; not a developer tutorial.
SOURCE: current snapshot sqlite_master + PRAGMA table_info. Semantic guidance: docs/DATABASES.md.
CONFIDENCE: V=sample/reference verified; N=name inference; U=unknown. Never present N/U as fact.

## HARD_CONSTRAINTS

- Numeric IDs/rowid are database-local. NEVER join numeric IDs across databases.
- Cross-DB joins must resolve to username/user_name first.
- SessionTable.summary is a summary, not complete message content.
- Msg_<md5> is a per-conversation table; observed mapping is MD5(username UTF-8), but revalidate after WeChat upgrades.
- WCDB_CT_message_content/source=4 marks corresponding Zstd BLOB in verified samples.
- Any BLOB may be probed as Zstd; failed probe means preserve/render as hex.
- FTS virtual tables may fail without MMFtsTokenizer; use shadow *_content tables when appropriate.
- Schema proves shape only. It does not prove semantic relationships or enum meanings.

## CORE_RELATIONS

```text
contact.contact(username|alias|remark|nick_name) -> username
username -> message_0.Name2Id.user_name -> rowid/is_session
username -> table Msg_ + MD5(username UTF-8)
Msg_*.real_sender_id -> SAME_DB Name2Id.rowid -> Name2Id.user_name
contact.chat_room.id -> contact.chatroom_member.room_id
contact.chatroom_member.member_id -> contact.contact.id
session.SessionUnreadListTable_1.username_id -> session.Name2Id.rowid
message_resource.MessageResourceInfo.chat_id -> SAME_DB ChatName2Id.rowid
message_resource.MessageResourceInfo.sender_id -> SAME_DB SenderName2Id.rowid
message_resource.MessageResourceDetail.message_id -> MessageResourceInfo.message_id
media.VoiceInfo.chat_name_id -> media.Name2Id.rowid
hardlink.*_hardlink_info_v4.dir2 -> hardlink.dir2id.rowid
favorite.fav_db_item.fromusr_id/realchatname_id -> favorite.Name2Id.rowid
```

## RETRIEVAL

- Exact machine schema: `docs/ai/database_schema.json`.
- APIs: `/api/databases`; `/api/tables?database=...`; `/api/schema?database=...`; `/api/schema/:table?database=...`; `/api/tables/:table?database=...&page=1&pageSize=20`.
- Compact column token: `cid:name:type:NN:PKn:default => meaning[confidence]`; `-` means absent/false/null.

## SNAPSHOT databases=18 tables=496 column_defs=5756 root=local-data/db-storage

## DB `bizchat/bizchat.db` tables=4
ROLE: 企业微信/业务聊天相关联系人、群与名称映射（名称推测）。
TABLES: chat_group, my_user_info, name2id, user_info
G1 TABLES=chat_group
ROLE=位于 bizchat/bizchat.db 的业务或内部表；用途主要依据表名推测。
COLS=0:id:INTEGER:-:PK1:NULL => 记录标识；具体作用域需结合所在表确认[N]; 1:group_id:TEXT:-:-:NULL => 关联记录 ID；仅在当前数据库的 ID 空间内有效[N]; 2:brand_user_name:TEXT:-:-:NULL => 名称或显示名称[N]; 3:type:INTEGER:-:-:NULL => 类型枚举值[N]; 4:version:INTEGER:-:-:NULL => 数据结构或记录版本[N]; 5:bit_flag:INTEGER:-:-:NULL => 位标志集合[N]; 6:max_member_count:INTEGER:-:-:NULL => 数量或计数[N]; 7:chat_name:TEXT:-:-:NULL => 名称或显示名称[N]; 8:owner_id:TEXT:-:-:NULL => 关联记录 ID；仅在当前数据库的 ID 空间内有效[N]; 9:head_img_url:TEXT:-:-:NULL => 头像图片 URL[N]; 10:user_list:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 11:add_member_url:TEXT:-:-:NULL => 网络资源地址[N]; 12:reserved0:INTEGER:-:-:NULL => 保留字段，实际用途待样本或代码验证[U]; 13:reserved1:INTEGER:-:-:NULL => 保留字段，实际用途待样本或代码验证[U]; 14:reserved2:TEXT:-:-:NULL => 保留字段，实际用途待样本或代码验证[U]; 15:reserved3:TEXT:-:-:NULL => 保留字段，实际用途待样本或代码验证[U]
G2 TABLES=my_user_info
ROLE=位于 bizchat/bizchat.db 的业务或内部表；用途主要依据表名推测。
COLS=0:brand_user_name:TEXT:-:PK1:NULL => 名称或显示名称[N]; 1:user_id:TEXT:-:-:NULL => 关联记录 ID；仅在当前数据库的 ID 空间内有效[N]
G3 TABLES=name2id
ROLE=名称到当前数据库内部 rowid 的映射表；不可直接跨库连接。
COLS=0:username:TEXT:-:PK1:NULL => 微信内部用户名/会话唯一标识[V]
G4 TABLES=user_info
ROLE=位于 bizchat/bizchat.db 的业务或内部表；用途主要依据表名推测。
COLS=0:id:INTEGER:-:PK1:NULL => 记录标识；具体作用域需结合所在表确认[N]; 1:user_id:TEXT:-:-:NULL => 关联记录 ID；仅在当前数据库的 ID 空间内有效[N]; 2:brand_user_name:TEXT:-:-:NULL => 名称或显示名称[N]; 3:user_name:TEXT:-:-:NULL => 微信内部用户名；常用于名称映射[N]; 4:version:INTEGER:-:-:NULL => 数据结构或记录版本[N]; 5:bit_flag:INTEGER:-:-:NULL => 位标志集合[N]; 6:head_img_url:TEXT:-:-:NULL => 头像图片 URL[N]; 7:profile_url:TEXT:-:-:NULL => 网络资源地址[N]; 8:add_member_url:TEXT:-:-:NULL => 网络资源地址[N]; 9:reserved0:INTEGER:-:-:NULL => 保留字段，实际用途待样本或代码验证[U]; 10:reserved1:INTEGER:-:-:NULL => 保留字段，实际用途待样本或代码验证[U]; 11:reserved2:TEXT:-:-:NULL => 保留字段，实际用途待样本或代码验证[U]; 12:reserved3:TEXT:-:-:NULL => 保留字段，实际用途待样本或代码验证[U]

## DB `contact/contact.db` tables=15
ROLE: 联系人、群聊和群成员主数据（已结合参考指南与实际表名）。
TABLES: biz_info, chat_room, chat_room_info_detail, chatroom_member, contact, contact_label, encrypt_name2id, name2id, openim_acct_type, openim_appid, openim_wording, oplog, stranger, stranger_ticket_info, ticket_info
G1 TABLES=biz_info
ROLE=位于 contact/contact.db 的业务或内部表；用途主要依据表名推测。
COLS=0:id:INTEGER:-:PK1:NULL => 记录标识；具体作用域需结合所在表确认[N]; 1:username:TEXT:-:-:NULL => 微信内部用户名/会话唯一标识[V]; 2:type:INTEGER:-:-:NULL => 类型枚举值[N]; 3:accept_type:INTEGER:-:-:NULL => 类型枚举值[N]; 4:child_type:INTEGER:-:-:NULL => 类型枚举值[N]; 5:home_url:TEXT:-:-:NULL => 网络资源地址[N]; 6:version:INTEGER:-:-:NULL => 数据结构或记录版本[N]; 7:external_info:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 8:brand_info:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 9:brand_icon_url:TEXT:-:-:NULL => 网络资源地址[N]; 10:brand_list:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 11:brand_flag:INTEGER:-:-:NULL => 位标志集合[N]; 12:belong:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 13:ext_buffer:BLOB:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 14:sync_version:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]
G2 TABLES=chat_room
ROLE=群聊或群成员关系表。
COLS=0:id:INTEGER:-:PK1:NULL => 记录标识；具体作用域需结合所在表确认[N]; 1:username:TEXT:-:-:NULL => 微信内部用户名/会话唯一标识[V]; 2:owner:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 3:ext_buffer:BLOB:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]
G3 TABLES=chat_room_info_detail
ROLE=位于 contact/contact.db 的业务或内部表；用途主要依据表名推测。
COLS=0:room_id_:INTEGER:-:PK1:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 1:username_:TEXT:-:-:NULL => 名称或显示名称[N]; 2:announcement_:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 3:announcement_editor_:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 4:announcement_publish_time_:INTEGER:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 5:chat_room_status_:INTEGER:-:-:NULL => 状态枚举值[N]; 6:xml_announcement_:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 7:ext_buffer_:BLOB:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]
G4 TABLES=chatroom_member
ROLE=群聊或群成员关系表。
COLS=0:room_id:INTEGER:-:-:NULL => 群聊记录 ID[V]; 1:member_id:INTEGER:-:-:NULL => 群成员联系人 ID[V]
G5 TABLES=contact,stranger
ROLE=联系人主表，包含内部 username、微信号、备注和昵称等。
COLS=0:id:INTEGER:-:PK1:NULL => 记录标识；具体作用域需结合所在表确认[N]; 1:username:TEXT:-:-:NULL => 微信内部用户名/会话唯一标识[V]; 2:local_type:INTEGER:-:-:NULL => 当前表定义的局部记录类型；枚举含义待验证[N]; 3:alias:TEXT:-:-:NULL => 用户设置的微信号[V]; 4:encrypt_username:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 5:flag:INTEGER:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 6:delete_flag:INTEGER:-:-:NULL => 位标志集合[N]; 7:verify_flag:INTEGER:-:-:NULL => 位标志集合[N]; 8:remark:TEXT:-:-:NULL => 本地备注名[V]; 9:remark_quan_pin:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 10:remark_pin_yin_initial:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 11:nick_name:TEXT:-:-:NULL => 昵称或群名[V]; 12:pin_yin_initial:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 13:quan_pin:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 14:big_head_url:TEXT:-:-:NULL => 网络资源地址[N]; 15:small_head_url:TEXT:-:-:NULL => 网络资源地址[N]; 16:head_img_md5:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 17:chat_room_notify:INTEGER:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 18:is_in_chat_room:INTEGER:-:-:NULL => 布尔或状态标记[N]; 19:description:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 20:extra_buffer:BLOB:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 21:chat_room_type:INTEGER:-:-:NULL => 类型枚举值[N]
G6 TABLES=contact_label
ROLE=位于 contact/contact.db 的业务或内部表；用途主要依据表名推测。
COLS=0:label_id_:INTEGER:-:PK1:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 1:label_name_:TEXT:-:-:NULL => 名称或显示名称[N]; 2:sort_order_:INTEGER:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]
G7 TABLES=encrypt_name2id,name2id
ROLE=位于 contact/contact.db 的业务或内部表；用途主要依据表名推测。
COLS=0:username:TEXT:-:PK1:NULL => 微信内部用户名/会话唯一标识[V]
G8 TABLES=openim_acct_type
ROLE=位于 contact/contact.db 的业务或内部表；用途主要依据表名推测。
COLS=0:lang_id:INTEGER:-:PK2:NULL => 关联记录 ID；仅在当前数据库的 ID 空间内有效[N]; 1:acc_type_id:TEXT:-:PK1:NULL => 关联记录 ID；仅在当前数据库的 ID 空间内有效[N]; 2:update_time:INTEGER:-:-:NULL => 更新时间，通常为 Unix 时间戳[N]; 3:ext_buffer:BLOB:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]
G9 TABLES=openim_appid
ROLE=位于 contact/contact.db 的业务或内部表；用途主要依据表名推测。
COLS=0:lang_id:INTEGER:-:PK2:NULL => 关联记录 ID；仅在当前数据库的 ID 空间内有效[N]; 1:app_id:TEXT:-:PK1:NULL => 关联记录 ID；仅在当前数据库的 ID 空间内有效[N]; 2:acct_type_id:TEXT:-:-:NULL => 关联记录 ID；仅在当前数据库的 ID 空间内有效[N]; 3:update_time:INTEGER:-:-:NULL => 更新时间，通常为 Unix 时间戳[N]; 4:ext_buffer:BLOB:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]
G10 TABLES=openim_wording
ROLE=位于 contact/contact.db 的业务或内部表；用途主要依据表名推测。
COLS=0:lang_id:INTEGER:-:PK2:NULL => 关联记录 ID；仅在当前数据库的 ID 空间内有效[N]; 1:app_id:TEXT:-:PK1:NULL => 关联记录 ID；仅在当前数据库的 ID 空间内有效[N]; 2:wording_id:TEXT:-:PK3:NULL => 关联记录 ID；仅在当前数据库的 ID 空间内有效[N]; 3:wording:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 4:pinyin:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 5:quan_pin:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 6:update_time:INTEGER:-:-:NULL => 更新时间，通常为 Unix 时间戳[N]; 7:ext_buffer:BLOB:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]
G11 TABLES=oplog
ROLE=位于 contact/contact.db 的业务或内部表；用途主要依据表名推测。
COLS=0:id:INTEGER:-:PK1:NULL => 记录标识；具体作用域需结合所在表确认[N]; 1:buffer:BLOB:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]
G12 TABLES=stranger_ticket_info,ticket_info
ROLE=位于 contact/contact.db 的业务或内部表；用途主要依据表名推测。
COLS=0:id:INTEGER:-:PK1:NULL => 记录标识；具体作用域需结合所在表确认[N]; 1:ticket:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]

## DB `contact/contact_fts.db` tables=37
ROLE: 联系人全文检索派生索引；部分虚拟表依赖微信私有分词器。
TABLES: chatroom_member_fts_v3, chatroom_member_fts_v3_aux, chatroom_member_fts_v3_config, chatroom_member_fts_v3_content, chatroom_member_fts_v3_data, chatroom_member_fts_v3_docsize, chatroom_member_fts_v3_idx, contact_fts_pinyin_v5, contact_fts_pinyin_v5_config, contact_fts_pinyin_v5_data, contact_fts_pinyin_v5_docsize, contact_fts_pinyin_v5_idx, contact_fts_v5, contact_fts_v5_config, contact_fts_v5_content, contact_fts_v5_data, contact_fts_v5_docsize, contact_fts_v5_idx, db_info, name2id, search_dict_fts_v1, search_dict_fts_v1_config, search_dict_fts_v1_data, search_dict_fts_v1_docsize, search_dict_fts_v1_idx, search_dict_v1, wa_contact_fts_pinyin_v1, wa_contact_fts_pinyin_v1_config, wa_contact_fts_pinyin_v1_data, wa_contact_fts_pinyin_v1_docsize, wa_contact_fts_pinyin_v1_idx, wa_contact_fts_v1, wa_contact_fts_v1_config, wa_contact_fts_v1_content, wa_contact_fts_v1_data, wa_contact_fts_v1_docsize, wa_contact_fts_v1_idx
G1 TABLES=chatroom_member_fts_v3,contact_fts_pinyin_v5,contact_fts_v5,search_dict_fts_v1,wa_contact_fts_pinyin_v1,wa_contact_fts_v1
ROLE=全文检索相关虚拟表或范围索引；可能依赖微信私有分词器。
COLS=
ERROR=no such tokenizer: MMFtsTokenizer
G2 TABLES=chatroom_member_fts_v3_aux
ROLE=全文检索相关虚拟表或范围索引；可能依赖微信私有分词器。
COLS=0:room_id:INTEGER:-:-:NULL => 群聊记录 ID[V]; 1:member_id:INTEGER:-:-:NULL => 群成员联系人 ID[V]
G3 TABLES=chatroom_member_fts_v3_config,contact_fts_pinyin_v5_config,contact_fts_v5_config,search_dict_fts_v1_config,wa_contact_fts_pinyin_v1_config,wa_contact_fts_v1_config
ROLE=全文检索虚拟表的影子/内部表，不应视为独立业务实体。
COLS=0:k:-:NN:PK1:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 1:v:-:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]
G4 TABLES=chatroom_member_fts_v3_content
ROLE=全文检索虚拟表的影子/内部表，不应视为独立业务实体。
COLS=0:id:INTEGER:-:PK1:NULL => 记录标识；具体作用域需结合所在表确认[N]; 1:c0:-:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 2:c1:-:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 3:c2:-:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]
G5 TABLES=chatroom_member_fts_v3_data,contact_fts_pinyin_v5_data,contact_fts_v5_data,search_dict_fts_v1_data,wa_contact_fts_pinyin_v1_data,wa_contact_fts_v1_data
ROLE=全文检索虚拟表的影子/内部表，不应视为独立业务实体。
COLS=0:id:INTEGER:-:PK1:NULL => 记录标识；具体作用域需结合所在表确认[N]; 1:block:BLOB:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]
G6 TABLES=chatroom_member_fts_v3_docsize,contact_fts_pinyin_v5_docsize,contact_fts_v5_docsize,search_dict_fts_v1_docsize,wa_contact_fts_pinyin_v1_docsize,wa_contact_fts_v1_docsize
ROLE=全文检索虚拟表的影子/内部表，不应视为独立业务实体。
COLS=0:id:INTEGER:-:PK1:NULL => 记录标识；具体作用域需结合所在表确认[N]; 1:sz:BLOB:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]
G7 TABLES=chatroom_member_fts_v3_idx,contact_fts_pinyin_v5_idx,contact_fts_v5_idx,search_dict_fts_v1_idx,wa_contact_fts_pinyin_v1_idx,wa_contact_fts_v1_idx
ROLE=全文检索虚拟表的影子/内部表，不应视为独立业务实体。
COLS=0:segid:-:NN:PK1:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 1:term:-:NN:PK2:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 2:pgno:-:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]
G8 TABLES=contact_fts_v5_content,wa_contact_fts_v1_content
ROLE=全文检索虚拟表的影子/内部表，不应视为独立业务实体。
COLS=0:id:INTEGER:-:PK1:NULL => 记录标识；具体作用域需结合所在表确认[N]; 1:c0:-:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 2:c1:-:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]
G9 TABLES=db_info
ROLE=位于 contact/contact_fts.db 的业务或内部表；用途主要依据表名推测。
COLS=0:Key:TEXT:-:PK1:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 1:ValueInt64:INTEGER:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 2:ValueDouble:REAL:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 3:ValueStdStr:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 4:ValueBlob:BLOB:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]
G10 TABLES=name2id
ROLE=名称到当前数据库内部 rowid 的映射表；不可直接跨库连接。
COLS=0:username:TEXT:-:PK1:NULL => 微信内部用户名/会话唯一标识[V]
G11 TABLES=search_dict_v1
ROLE=位于 contact/contact_fts.db 的业务或内部表；用途主要依据表名推测。
COLS=0:content:TEXT:-:PK1:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 1:update_time:INTEGER:-:-:NULL => 更新时间，通常为 Unix 时间戳[N]; 2:click_type:INTEGER:-:-:NULL => 类型枚举值[N]

## DB `emoticon/emoticon.db` tables=7
ROLE: 表情、自定义表情和表情包元数据（名称推测）。
TABLES: kCustomEmoticonOrderTable, kExpressRecentUseEemoticonTable, kFavEmoticonOrderTable, kNonStoreEmoticonTable, kStoreEmoticonCaptionsTable, kStoreEmoticonFilesTable, kStoreEmoticonPackageTable
G1 TABLES=kCustomEmoticonOrderTable,kFavEmoticonOrderTable
ROLE=位于 emoticon/emoticon.db 的业务或内部表；用途主要依据表名推测。
COLS=0:md5:TEXT:-:-:NULL => 内容或标识的 MD5 摘要[N]
G2 TABLES=kExpressRecentUseEemoticonTable
ROLE=位于 emoticon/emoticon.db 的业务或内部表；用途主要依据表名推测。
COLS=0:Key:TEXT:-:PK1:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 1:ValueInt64:INTEGER:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 2:ValueDouble:REAL:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 3:ValueStdStr:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 4:ValueBlob:BLOB:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]
G3 TABLES=kNonStoreEmoticonTable
ROLE=位于 emoticon/emoticon.db 的业务或内部表；用途主要依据表名推测。
COLS=0:type:INTEGER:-:-:NULL => 类型枚举值[N]; 1:md5:TEXT:-:-:NULL => 内容或标识的 MD5 摘要[N]; 2:caption:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 3:product_id:TEXT:-:-:NULL => 关联记录 ID；仅在当前数据库的 ID 空间内有效[N]; 4:aes_key:TEXT:-:-:NULL => 键值、索引键或业务键[N]; 5:thumb_url:TEXT:-:-:NULL => 网络资源地址[N]; 6:tp_url:TEXT:-:-:NULL => 网络资源地址[N]; 7:auth_key:TEXT:-:-:NULL => 键值、索引键或业务键[N]; 8:cdn_url:TEXT:-:-:NULL => 网络资源地址[N]; 9:extern_url:TEXT:-:-:NULL => 网络资源地址[N]; 10:extern_md5:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 11:encrypt_url:TEXT:-:-:NULL => 网络资源地址[N]
G4 TABLES=kStoreEmoticonCaptionsTable
ROLE=位于 emoticon/emoticon.db 的业务或内部表；用途主要依据表名推测。
COLS=0:package_id_:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 1:md5_:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 2:language_:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 3:caption_:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]
G5 TABLES=kStoreEmoticonFilesTable
ROLE=位于 emoticon/emoticon.db 的业务或内部表；用途主要依据表名推测。
COLS=0:package_id_:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 1:md5_:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 2:type_:INTEGER:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 3:sort_order_:INTEGER:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 4:emoticon_size_:INTEGER:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 5:emoticon_offset_:INTEGER:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 6:thumb_size_:INTEGER:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 7:thumb_offset_:INTEGER:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]
G6 TABLES=kStoreEmoticonPackageTable
ROLE=位于 emoticon/emoticon.db 的业务或内部表；用途主要依据表名推测。
COLS=0:package_id_:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 1:package_name_:TEXT:-:-:NULL => 名称或显示名称[N]; 2:payment_status_:INTEGER:-:-:NULL => 状态枚举值[N]; 3:download_status_:INTEGER:-:-:NULL => 状态枚举值[N]; 4:install_time_:INTEGER:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 5:remove_time_:INTEGER:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 6:sort_order_:INTEGER:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 7:introduction_:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 8:full_description_:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 9:copyright_:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 10:author_:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 11:store_icon_url_:TEXT:-:-:NULL => 网络资源地址[N]; 12:panel_url_:TEXT:-:-:NULL => 网络资源地址[N]

## DB `favorite/favorite.db` tables=6
ROLE: 收藏条目及名称映射（参考指南）。
TABLES: Name2Id, buff, config, fav_bind_tag_db_item, fav_db_item, fav_tag_db_item
G1 TABLES=Name2Id
ROLE=名称到当前数据库内部 rowid 的映射表；不可直接跨库连接。
COLS=0:user_name:TEXT:-:PK1:NULL => 微信内部用户名；常用于名称映射[N]
G2 TABLES=buff,config
ROLE=位于 favorite/favorite.db 的业务或内部表；用途主要依据表名推测。
COLS=0:Key:TEXT:-:PK1:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 1:ValueInt64:INTEGER:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 2:ValueDouble:REAL:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 3:ValueStdStr:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 4:ValueBlob:BLOB:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]
G3 TABLES=fav_bind_tag_db_item
ROLE=位于 favorite/favorite.db 的业务或内部表；用途主要依据表名推测。
COLS=0:tag_local_id:INTEGER:-:-:NULL => 关联记录 ID；仅在当前数据库的 ID 空间内有效[N]; 1:tag_server_id:INTEGER:-:-:NULL => 关联记录 ID；仅在当前数据库的 ID 空间内有效[N]; 2:fav_local_id:INTEGER:-:-:NULL => 关联记录 ID；仅在当前数据库的 ID 空间内有效[N]; 3:fav_server_id:INTEGER:-:-:NULL => 关联记录 ID；仅在当前数据库的 ID 空间内有效[N]; 4:op_code:INTEGER:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]
G4 TABLES=fav_db_item
ROLE=位于 favorite/favorite.db 的业务或内部表；用途主要依据表名推测。
COLS=0:local_id:INTEGER:-:PK1:NULL => 本地消息或业务记录 ID[V]; 1:server_id:INTEGER:-:-:NULL => 服务端消息 ID[V]; 2:type:INTEGER:-:-:NULL => 类型枚举值[N]; 3:update_seq:INTEGER:-:-:NULL => 排序或递增序列[N]; 4:flag:INTEGER:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 5:update_time:INTEGER:-:-:NULL => 更新时间，通常为 Unix 时间戳[N]; 6:version:INTEGER:-:-:NULL => 数据结构或记录版本[N]; 7:content:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 8:source_id:TEXT:-:-:NULL => 关联记录 ID；仅在当前数据库的 ID 空间内有效[N]; 9:sync_status:INTEGER:-:-:NULL => 状态枚举值[N]; 10:upload_status:INTEGER:-:-:NULL => 状态枚举值[N]; 11:upload_error_code:INTEGER:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 12:trans_res_status:INTEGER:-:-:NULL => 状态枚举值[N]; 13:trans_res_error_code:INTEGER:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 14:fromusr:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 15:fromusr_id:INTEGER:-:-:NULL => 关联记录 ID；仅在当前数据库的 ID 空间内有效[N]; 16:realchatname:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 17:realchatname_id:INTEGER:-:-:NULL => 关联记录 ID；仅在当前数据库的 ID 空间内有效[N]; 18:ext_buf:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]
G5 TABLES=fav_tag_db_item
ROLE=位于 favorite/favorite.db 的业务或内部表；用途主要依据表名推测。
COLS=0:local_id:INTEGER:-:PK1:NULL => 本地消息或业务记录 ID[V]; 1:server_id:INTEGER:-:-:NULL => 服务端消息 ID[V]; 2:name:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 3:seq:INTEGER:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]

## DB `favorite/favorite_fts.db` tables=7
ROLE: 收藏全文检索派生索引（名称推测）。
TABLES: fav_fts_v1, fav_fts_v1_config, fav_fts_v1_content, fav_fts_v1_data, fav_fts_v1_docsize, fav_fts_v1_idx, table_info
G1 TABLES=fav_fts_v1
ROLE=全文检索相关虚拟表或范围索引；可能依赖微信私有分词器。
COLS=
ERROR=no such tokenizer: MMFtsTokenizer
G2 TABLES=fav_fts_v1_config
ROLE=全文检索虚拟表的影子/内部表，不应视为独立业务实体。
COLS=0:k:-:NN:PK1:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 1:v:-:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]
G3 TABLES=fav_fts_v1_content
ROLE=全文检索虚拟表的影子/内部表，不应视为独立业务实体。
COLS=0:id:INTEGER:-:PK1:NULL => 记录标识；具体作用域需结合所在表确认[N]; 1:c0:-:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 2:c1:-:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 3:c2:-:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 4:c3:-:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]
G4 TABLES=fav_fts_v1_data
ROLE=全文检索虚拟表的影子/内部表，不应视为独立业务实体。
COLS=0:id:INTEGER:-:PK1:NULL => 记录标识；具体作用域需结合所在表确认[N]; 1:block:BLOB:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]
G5 TABLES=fav_fts_v1_docsize
ROLE=全文检索虚拟表的影子/内部表，不应视为独立业务实体。
COLS=0:id:INTEGER:-:PK1:NULL => 记录标识；具体作用域需结合所在表确认[N]; 1:sz:BLOB:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]
G6 TABLES=fav_fts_v1_idx
ROLE=全文检索虚拟表的影子/内部表，不应视为独立业务实体。
COLS=0:segid:-:NN:PK1:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 1:term:-:NN:PK2:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 2:pgno:-:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]
G7 TABLES=table_info
ROLE=位于 favorite/favorite_fts.db 的业务或内部表；用途主要依据表名推测。
COLS=0:Key:TEXT:-:PK1:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 1:ValueInt64:INTEGER:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 2:ValueDouble:REAL:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 3:ValueStdStr:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 4:ValueBlob:BLOB:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]

## DB `general/general.db` tables=22
ROLE: 撤回、转账、红包、最近联系人等通用业务状态（参考指南）。
TABLES: FMessageTable, ForwardRecent, SearchRecent, WeAppBizAttrSyncBufferTableV02, biz_pay_status, biz_subscribe_status, brand_search_record, groupPayTable, handoff_remind_v0, ilink_voip, redEnvelopeTable, reddot, reddot_last_notify, reddot_record, revokebatchmessage, revokemessage, teenager_apply_access_agree_info, transferTable, wacontact, wcfinderlivestatus, wcfinderuserpage, websearch_record
G1 TABLES=FMessageTable
ROLE=位于 general/general.db 的业务或内部表；用途主要依据表名推测。
COLS=0:user_name_:TEXT:-:-:NULL => 微信内部用户名（尾部下划线为表内命名约定）[N]; 1:type_:INTEGER:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 2:timestamp_:INTEGER:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 3:encrypt_user_name_:TEXT:-:-:NULL => 名称或显示名称[N]; 4:content_:TEXT:-:-:NULL => 正文或内容数据[N]; 5:is_sender_:INTEGER:-:-:NULL => 布尔或状态标记[N]; 6:ticket_:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 7:scene_:INTEGER:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 8:fmessage_detail_buf_:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 9:remark_:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 10:label_ids_:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]
G2 TABLES=ForwardRecent
ROLE=位于 general/general.db 的业务或内部表；用途主要依据表名推测。
COLS=0:username:TEXT:-:-:NULL => 微信内部用户名/会话唯一标识[V]; 1:forward_time:INTEGER:-:-:NULL => 时间值；单位和时区需结合样本验证[N]
G3 TABLES=SearchRecent
ROLE=位于 general/general.db 的业务或内部表；用途主要依据表名推测。
COLS=0:username:TEXT:-:-:NULL => 微信内部用户名/会话唯一标识[V]; 1:query:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 2:score:INTEGER:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 3:last_click_time:INTEGER:-:-:NULL => 时间值；单位和时区需结合样本验证[N]
G4 TABLES=WeAppBizAttrSyncBufferTableV02
ROLE=位于 general/general.db 的业务或内部表；用途主要依据表名推测。
COLS=0:user_name:TEXT:-:PK1:NULL => 微信内部用户名；常用于名称映射[N]; 1:last_update_time:INTEGER:-:-:NULL => 时间值；单位和时区需结合样本验证[N]; 2:version:TEXT:-:-:NULL => 数据结构或记录版本[N]
G5 TABLES=biz_pay_status
ROLE=位于 general/general.db 的业务或内部表；用途主要依据表名推测。
COLS=0:url_id:TEXT:-:-:NULL => 关联记录 ID；仅在当前数据库的 ID 空间内有效[N]; 1:is_charge_appmsg:INTEGER:-:-:NULL => 布尔或状态标记[N]; 2:is_paid:INTEGER:-:-:NULL => 布尔或状态标记[N]; 3:friend_pay_count_str:TEXT:-:-:NULL => 数量或计数[N]
G6 TABLES=biz_subscribe_status
ROLE=位于 general/general.db 的业务或内部表；用途主要依据表名推测。
COLS=0:biz_username:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 1:template_id:TEXT:-:-:NULL => 关联记录 ID；仅在当前数据库的 ID 空间内有效[N]; 2:is_subscribe:INTEGER:-:-:NULL => 布尔或状态标记[N]
G7 TABLES=brand_search_record,websearch_record
ROLE=位于 general/general.db 的业务或内部表；用途主要依据表名推测。
COLS=0:keyword:TEXT:-:PK1:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 1:pay_load_:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 2:create_time:INTEGER:-:-:NULL => 创建时间，通常为 Unix 时间戳[V]
G8 TABLES=groupPayTable
ROLE=位于 general/general.db 的业务或内部表；用途主要依据表名推测。
COLS=0:bill_no:TEXT:-:PK1:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 1:session_name:TEXT:-:-:NULL => 名称或显示名称[N]; 2:message_local_id:INTEGER:-:-:NULL => 关联记录 ID；仅在当前数据库的 ID 空间内有效[N]; 3:message_create_time:INTEGER:-:-:NULL => 时间值；单位和时区需结合样本验证[N]
G9 TABLES=handoff_remind_v0
ROLE=位于 general/general.db 的业务或内部表；用途主要依据表名推测。
COLS=0:local_id:INTEGER:-:PK1:NULL => 本地消息或业务记录 ID[V]; 1:item_id:TEXT:-:-:NULL => 关联记录 ID；仅在当前数据库的 ID 空间内有效[N]; 2:head_icon:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 3:title:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 4:desc_type:TEXT:-:-:NULL => 类型枚举值[N]; 5:create_time:INTEGER:-:-:NULL => 创建时间，通常为 Unix 时间戳[V]; 6:start_time:INTEGER:-:-:NULL => 时间值；单位和时区需结合样本验证[N]; 7:expire_time:INTEGER:-:-:NULL => 时间值；单位和时区需结合样本验证[N]; 8:biz_type:INTEGER:-:-:NULL => 类型枚举值[N]; 9:version:INTEGER:-:-:NULL => 数据结构或记录版本[N]; 10:url:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 11:extra_info:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]
G10 TABLES=ilink_voip
ROLE=位于 general/general.db 的业务或内部表；用途主要依据表名推测。
COLS=0:wx_chatroom_:TEXT:-:PK1:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 1:millsecond_:INTEGER:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 2:group_id_:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 3:room_id_:INTEGER:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 4:room_key_:INTEGER:-:-:NULL => 键值、索引键或业务键[N]; 5:route_id_:INTEGER:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 6:voice_status_:INTEGER:-:-:NULL => 状态枚举值[N]; 7:talker_create_user_:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 8:not_friend_user_list_:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 9:members_:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 10:is_ilink_:INTEGER:-:-:NULL => 布尔或状态标记[N]; 11:ever_quit_chatroom_:INTEGER:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]
G11 TABLES=redEnvelopeTable
ROLE=红包相关记录。
COLS=0:message_server_id:INTEGER:-:PK1:NULL => 关联记录 ID；仅在当前数据库的 ID 空间内有效[N]; 1:session_name:TEXT:-:-:NULL => 名称或显示名称[N]; 2:sender_user_name:TEXT:-:-:NULL => 名称或显示名称[N]; 3:native_url:TEXT:-:-:NULL => 网络资源地址[N]; 4:send_id:TEXT:-:-:NULL => 关联记录 ID；仅在当前数据库的 ID 空间内有效[N]; 5:scene_id:INTEGER:-:-:NULL => 关联记录 ID；仅在当前数据库的 ID 空间内有效[N]; 6:hb_status:INTEGER:-:-:NULL => 状态枚举值[N]; 7:hb_type:INTEGER:-:-:NULL => 类型枚举值[N]; 8:receive_status:INTEGER:-:-:NULL => 状态枚举值[N]
G12 TABLES=reddot,reddot_last_notify
ROLE=位于 general/general.db 的业务或内部表；用途主要依据表名推测。
COLS=0:tips_uuid:TEXT:-:PK1:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 1:tips_content:TEXT:-:-:NULL => 正文或内容数据[N]
G13 TABLES=reddot_record
ROLE=位于 general/general.db 的业务或内部表；用途主要依据表名推测。
COLS=0:business_type:INTEGER:-:PK1:NULL => 类型枚举值[N]; 1:record_content:TEXT:-:-:NULL => 正文或内容数据[N]
G14 TABLES=revokebatchmessage
ROLE=消息撤回记录。
COLS=0:local_id:INTEGER:-:PK1:NULL => 本地消息或业务记录 ID[V]; 1:batch_id:INTEGER:-:-:NULL => 关联记录 ID；仅在当前数据库的 ID 空间内有效[N]; 2:msg_unique_id:TEXT:-:-:NULL => 关联记录 ID；仅在当前数据库的 ID 空间内有效[N]; 3:session_name:TEXT:-:-:NULL => 名称或显示名称[N]; 4:msg_local_id:INTEGER:-:-:NULL => 关联记录 ID；仅在当前数据库的 ID 空间内有效[N]; 5:msg_create_time:INTEGER:-:-:NULL => 时间值；单位和时区需结合样本验证[N]
G15 TABLES=revokemessage
ROLE=消息撤回记录。
COLS=0:to_user_name:TEXT:-:-:NULL => 名称或显示名称[N]; 1:svr_id:INTEGER:-:-:NULL => 关联记录 ID；仅在当前数据库的 ID 空间内有效[N]; 2:message_type:INTEGER:-:-:NULL => 类型枚举值[N]; 3:revoke_time:INTEGER:-:-:NULL => 时间值；单位和时区需结合样本验证[N]; 4:content:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 5:at_user_list:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]
G16 TABLES=teenager_apply_access_agree_info
ROLE=位于 general/general.db 的业务或内部表；用途主要依据表名推测。
COLS=0:access_content_key:TEXT:-:PK1:NULL => 正文或内容数据[N]; 1:access_content_type:INTEGER:-:-:NULL => 类型枚举值[N]; 2:agree_time:INTEGER:-:-:NULL => 时间值；单位和时区需结合样本验证[N]
G17 TABLES=transferTable
ROLE=转账相关记录。
COLS=0:transfer_id:TEXT:-:PK1:NULL => 关联记录 ID；仅在当前数据库的 ID 空间内有效[N]; 1:transcation_id:TEXT:-:-:NULL => 关联记录 ID；仅在当前数据库的 ID 空间内有效[N]; 2:message_server_id:INTEGER:-:-:NULL => 关联记录 ID；仅在当前数据库的 ID 空间内有效[N]; 3:second_message_server_id:INTEGER:-:-:NULL => 关联记录 ID；仅在当前数据库的 ID 空间内有效[N]; 4:session_name:TEXT:-:-:NULL => 名称或显示名称[N]; 5:pay_sub_type:INTEGER:-:-:NULL => 类型枚举值[N]; 6:pay_receiver:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 7:pay_payer:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 8:begin_transfer_time:INTEGER:-:-:NULL => 时间值；单位和时区需结合样本验证[N]; 9:last_modified_time:INTEGER:-:-:NULL => 时间值；单位和时区需结合样本验证[N]; 10:invalid_time:INTEGER:-:-:NULL => 时间值；单位和时区需结合样本验证[N]; 11:last_update_time:INTEGER:-:-:NULL => 时间值；单位和时区需结合样本验证[N]; 12:delay_confirm_flag:INTEGER:-:-:NULL => 位标志集合[N]; 13:bubble_clicked_flag:INTEGER:-:-:NULL => 位标志集合[N]
G18 TABLES=wacontact
ROLE=位于 general/general.db 的业务或内部表；用途主要依据表名推测。
COLS=0:user_name:TEXT:-:PK1:NULL => 微信内部用户名；常用于名称映射[N]; 1:type:INTEGER:-:-:NULL => 类型枚举值[N]; 2:brand_icon_url:TEXT:-:-:NULL => 网络资源地址[N]; 3:external_info:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 4:contact_pack_data:BLOB:-:-:NULL => 二进制或序列化数据，编码需进一步确认[N]; 5:wx_app_opt:INTEGER:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 6:head_image_status:TEXT:-:-:NULL => 状态枚举值[N]; 7:app_id:TEXT:-:-:NULL => 关联记录 ID；仅在当前数据库的 ID 空间内有效[N]
G19 TABLES=wcfinderlivestatus
ROLE=位于 general/general.db 的业务或内部表；用途主要依据表名推测。
COLS=0:finder_live_id:INTEGER:-:-:NULL => 关联记录 ID；仅在当前数据库的 ID 空间内有效[N]; 1:finder_username:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 2:finder_export_id:TEXT:-:-:NULL => 关联记录 ID；仅在当前数据库的 ID 空间内有效[N]; 3:live_status:INTEGER:-:-:NULL => 状态枚举值[N]; 4:replay_status:INTEGER:-:-:NULL => 状态枚举值[N]; 5:charge_flag:INTEGER:-:-:NULL => 位标志集合[N]
G20 TABLES=wcfinderuserpage
ROLE=位于 general/general.db 的业务或内部表；用途主要依据表名推测。
COLS=0:username:TEXT:-:-:NULL => 微信内部用户名/会话唯一标识[V]; 1:extra_buffer:BLOB:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]

## DB `hardlink/hardlink.db` tables=8
ROLE: 图片、视频、文件的本地硬链接索引（参考指南）。
TABLES: db_info, dir2id, file_checkpoint_v4, file_hardlink_info_v4, image_hardlink_info_v4, talker_checkpoint_v4, video_checkpoint_v4, video_hardlink_info_v4
G1 TABLES=db_info
ROLE=位于 hardlink/hardlink.db 的业务或内部表；用途主要依据表名推测。
COLS=0:Key:TEXT:-:PK1:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 1:ValueInt64:INTEGER:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 2:ValueDouble:REAL:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 3:ValueStdStr:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 4:ValueBlob:BLOB:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]
G2 TABLES=dir2id
ROLE=名称到当前数据库内部 rowid 的映射表；不可直接跨库连接。
COLS=0:username:TEXT:-:PK1:NULL => 微信内部用户名/会话唯一标识[V]
G3 TABLES=file_checkpoint_v4,video_checkpoint_v4
ROLE=位于 hardlink/hardlink.db 的业务或内部表；用途主要依据表名推测。
COLS=0:month_id:INTEGER:-:PK1:NULL => 关联记录 ID；仅在当前数据库的 ID 空间内有效[N]
G4 TABLES=file_hardlink_info_v4,image_hardlink_info_v4,video_hardlink_info_v4
ROLE=本地媒体或文件硬链接索引。
COLS=0:md5_hash:INTEGER:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 1:md5:TEXT:-:-:NULL => 内容或标识的 MD5 摘要[N]; 2:type:INTEGER:-:-:NULL => 类型枚举值[N]; 3:file_name:TEXT:-:-:NULL => 名称或显示名称[N]; 4:file_size:INTEGER:-:-:NULL => 数据长度或尺寸[N]; 5:modify_time:INTEGER:-:-:NULL => 修改时间，通常为 Unix 时间戳[N]; 6:dir1:INTEGER:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 7:dir2:INTEGER:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 8:_rowid_:INTEGER:-:PK1:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 9:extra_buffer:BLOB:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]
G5 TABLES=talker_checkpoint_v4
ROLE=位于 hardlink/hardlink.db 的业务或内部表；用途主要依据表名推测。
COLS=0:talker_id:INTEGER:-:PK1:NULL => 关联记录 ID；仅在当前数据库的 ID 空间内有效[N]; 1:month_id:INTEGER:-:-:NULL => 关联记录 ID；仅在当前数据库的 ID 空间内有效[N]

## DB `head_image/head_image.db` tables=1
ROLE: 联系人或群的头像缓存索引（参考指南）。
TABLES: head_image
G1 TABLES=head_image
ROLE=位于 head_image/head_image.db 的业务或内部表；用途主要依据表名推测。
COLS=0:username:TEXT:-:PK1:NULL => 微信内部用户名/会话唯一标识[V]; 1:md5:TEXT:-:-:NULL => 内容或标识的 MD5 摘要[N]; 2:image_buffer:BLOB:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 3:update_time:INTEGER:-:-:NULL => 更新时间，通常为 Unix 时间戳[N]

## DB `message/biz_message_0.db` tables=142
ROLE: 公众号及业务消息分表（参考指南）。
TABLES: DeleteInfo, Msg_<md5>[count=138], Name2Id, TimeStamp, wcdb_builtin_compression_record
G1 TABLES=DeleteInfo
ROLE=删除记录或删除状态。
COLS=0:chat_name_id:INTEGER:-:-:NULL => 会话名在当前数据库名称映射表中的 ID[N]; 1:delete_table_name:TEXT:-:-:NULL => 名称或显示名称[N]
G2 TABLES=Msg_<md5>[count=138]
ROLE=某一会话的消息分表；后缀通常为会话 username 的 MD5。
COLS=0:local_id:INTEGER:-:PK1:NULL => 本地消息或业务记录 ID[V]; 1:server_id:INTEGER:-:-:NULL => 服务端消息 ID[V]; 2:local_type:INTEGER:-:-:NULL => 消息类型；低 32 位通常为基础类型[V]; 3:sort_seq:INTEGER:-:-:NULL => 消息排序序列[V]; 4:real_sender_id:INTEGER:-:-:NULL => 真实发送者在本数据库 Name2Id 表中的 rowid[V]; 5:create_time:INTEGER:-:-:NULL => 创建时间，通常为 Unix 时间戳[V]; 6:status:INTEGER:-:-:NULL => 状态枚举值[N]; 7:upload_status:INTEGER:-:-:NULL => 状态枚举值[N]; 8:download_status:INTEGER:-:-:NULL => 状态枚举值[N]; 9:server_seq:INTEGER:-:-:NULL => 排序或递增序列[N]; 10:origin_source:INTEGER:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 11:source:TEXT:-:-:NULL => 消息来源/msgsource 等附加信息；可能由 WCDB_CT_source 标记为 Zstd BLOB[V]; 12:message_content:TEXT:-:-:NULL => 消息正文；可能由 WCDB_CT_message_content 标记为 Zstd BLOB[V]; 13:compress_content:TEXT:-:-:NULL => 正文或内容数据[N]; 14:packed_info_data:BLOB:-:-:NULL => 未解析的打包扩展数据[V]; 15:WCDB_CT_message_content:INTEGER:-:-:NULL => message_content 的 WCDB 存储/压缩类型；值 4 已验证为 Zstd BLOB[V]; 16:WCDB_CT_source:INTEGER:-:-:NULL => source 的 WCDB 存储/压缩类型；值 4 已验证为 Zstd BLOB[V]
G3 TABLES=Name2Id
ROLE=名称到当前数据库内部 rowid 的映射表；不可直接跨库连接。
COLS=0:user_name:TEXT:-:PK1:NULL => 微信内部用户名；常用于名称映射[N]; 1:is_session:INTEGER:-:-:NULL => 名称映射项是否代表会话[V]
G4 TABLES=TimeStamp
ROLE=位于 message/biz_message_0.db 的业务或内部表；用途主要依据表名推测。
COLS=0:timestamp:INTEGER:-:-:NULL => 时间值；单位和时区需结合样本验证[N]
G5 TABLES=wcdb_builtin_compression_record
ROLE=位于 message/biz_message_0.db 的业务或内部表；用途主要依据表名推测。
COLS=0:tableName:TEXT:NN:PK1:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 1:columns:TEXT:NN:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 2:rowid:INTEGER:-:-:NULL => SQLite 行标识或关联行 ID[N]

## DB `message/media_0.db` tables=3
ROLE: 语音等消息媒体索引（参考指南）。
TABLES: Name2Id, TimeStamp, VoiceInfo
G1 TABLES=Name2Id
ROLE=名称到当前数据库内部 rowid 的映射表；不可直接跨库连接。
COLS=0:user_name:TEXT:-:PK1:NULL => 微信内部用户名；常用于名称映射[N]
G2 TABLES=TimeStamp
ROLE=位于 message/media_0.db 的业务或内部表；用途主要依据表名推测。
COLS=0:timestamp:INTEGER:-:-:NULL => 时间值；单位和时区需结合样本验证[N]
G3 TABLES=VoiceInfo
ROLE=位于 message/media_0.db 的业务或内部表；用途主要依据表名推测。
COLS=0:chat_name_id:INTEGER:-:-:NULL => 会话名在当前数据库名称映射表中的 ID[N]; 1:create_time:INTEGER:-:-:NULL => 创建时间，通常为 Unix 时间戳[V]; 2:local_id:INTEGER:-:-:NULL => 本地消息或业务记录 ID[V]; 3:svr_id:INTEGER:-:-:NULL => 关联记录 ID；仅在当前数据库的 ID 空间内有效[N]; 4:voice_data:BLOB:-:-:NULL => 二进制或序列化数据，编码需进一步确认[N]; 5:data_index:TEXT:-:-:'0' => 二进制或序列化数据，编码需进一步确认[N]

## DB `message/message_0.db` tables=156
ROLE: 普通聊天消息主库，按会话拆分为 Msg_* 表（参考指南）。
TABLES: DeleteInfo, Msg_<md5>[count=152], Name2Id, TimeStamp, wcdb_builtin_compression_record
G1 TABLES=DeleteInfo
ROLE=删除记录或删除状态。
COLS=0:chat_name_id:INTEGER:-:-:NULL => 会话名在当前数据库名称映射表中的 ID[N]; 1:delete_table_name:TEXT:-:-:NULL => 名称或显示名称[N]
G2 TABLES=Msg_<md5>[count=152]
ROLE=某一会话的消息分表；后缀通常为会话 username 的 MD5。
COLS=0:local_id:INTEGER:-:PK1:NULL => 本地消息或业务记录 ID[V]; 1:server_id:INTEGER:-:-:NULL => 服务端消息 ID[V]; 2:local_type:INTEGER:-:-:NULL => 消息类型；低 32 位通常为基础类型[V]; 3:sort_seq:INTEGER:-:-:NULL => 消息排序序列[V]; 4:real_sender_id:INTEGER:-:-:NULL => 真实发送者在本数据库 Name2Id 表中的 rowid[V]; 5:create_time:INTEGER:-:-:NULL => 创建时间，通常为 Unix 时间戳[V]; 6:status:INTEGER:-:-:NULL => 状态枚举值[N]; 7:upload_status:INTEGER:-:-:NULL => 状态枚举值[N]; 8:download_status:INTEGER:-:-:NULL => 状态枚举值[N]; 9:server_seq:INTEGER:-:-:NULL => 排序或递增序列[N]; 10:origin_source:INTEGER:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 11:source:TEXT:-:-:NULL => 消息来源/msgsource 等附加信息；可能由 WCDB_CT_source 标记为 Zstd BLOB[V]; 12:message_content:TEXT:-:-:NULL => 消息正文；可能由 WCDB_CT_message_content 标记为 Zstd BLOB[V]; 13:compress_content:TEXT:-:-:NULL => 正文或内容数据[N]; 14:packed_info_data:BLOB:-:-:NULL => 未解析的打包扩展数据[V]; 15:WCDB_CT_message_content:INTEGER:-:-:NULL => message_content 的 WCDB 存储/压缩类型；值 4 已验证为 Zstd BLOB[V]; 16:WCDB_CT_source:INTEGER:-:-:NULL => source 的 WCDB 存储/压缩类型；值 4 已验证为 Zstd BLOB[V]
G3 TABLES=Name2Id
ROLE=名称到当前数据库内部 rowid 的映射表；不可直接跨库连接。
COLS=0:user_name:TEXT:-:PK1:NULL => 微信内部用户名；常用于名称映射[N]; 1:is_session:INTEGER:-:-:NULL => 名称映射项是否代表会话[V]
G4 TABLES=TimeStamp
ROLE=位于 message/message_0.db 的业务或内部表；用途主要依据表名推测。
COLS=0:timestamp:INTEGER:-:-:NULL => 时间值；单位和时区需结合样本验证[N]
G5 TABLES=wcdb_builtin_compression_record
ROLE=位于 message/message_0.db 的业务或内部表；用途主要依据表名推测。
COLS=0:tableName:TEXT:NN:PK1:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 1:columns:TEXT:NN:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 2:rowid:INTEGER:-:-:NULL => SQLite 行标识或关联行 ID[N]

## DB `message/message_fts.db` tables=60
ROLE: 消息全文检索派生索引（参考指南）。
TABLES: ImgFts0V0, ImgFts0V0_config, ImgFts0V0_data, ImgFts0V0_docsize, ImgFts0V0_idx, ImgFts1V0, ImgFts1V0_config, ImgFts1V0_data, ImgFts1V0_docsize, ImgFts1V0_idx, ImgFts2V0, ImgFts2V0_config, ImgFts2V0_data, ImgFts2V0_docsize, ImgFts2V0_idx, ImgFts3V0, ImgFts3V0_config, ImgFts3V0_data, ImgFts3V0_docsize, ImgFts3V0_idx, ImgFtsAux0V0, ImgFtsAux1V0, ImgFtsAux2V0, ImgFtsAux3V0, ImgFtsV0, ImgRangeV0, ImgTableInfo, deleteImgFtsV0, message_fts_v4_0, message_fts_v4_0_config, message_fts_v4_0_content, message_fts_v4_0_data, message_fts_v4_0_docsize, message_fts_v4_0_idx, message_fts_v4_1, message_fts_v4_1_config, message_fts_v4_1_content, message_fts_v4_1_data, message_fts_v4_1_docsize, message_fts_v4_1_idx, message_fts_v4_2, message_fts_v4_2_config, message_fts_v4_2_content, message_fts_v4_2_data, message_fts_v4_2_docsize, message_fts_v4_2_idx, message_fts_v4_3, message_fts_v4_3_config, message_fts_v4_3_content, message_fts_v4_3_data, message_fts_v4_3_docsize, message_fts_v4_3_idx, message_fts_v4_aux_0, message_fts_v4_aux_1, message_fts_v4_aux_2, message_fts_v4_aux_3, message_fts_v4_range, message_fts_v4_session_delete_info, name2id, table_info
G1 TABLES=ImgFts0V0,ImgFts1V0,ImgFts2V0,ImgFts3V0,message_fts_v4_0,message_fts_v4_1,message_fts_v4_2,message_fts_v4_3
ROLE=全文检索相关虚拟表或范围索引；可能依赖微信私有分词器。
COLS=
ERROR=no such tokenizer: MMFtsTokenizer
G2 TABLES=ImgFts0V0_config,ImgFts1V0_config,ImgFts2V0_config,ImgFts3V0_config,message_fts_v4_0_config,message_fts_v4_1_config,message_fts_v4_2_config,message_fts_v4_3_config
ROLE=全文检索虚拟表的影子/内部表，不应视为独立业务实体。
COLS=0:k:-:NN:PK1:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 1:v:-:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]
G3 TABLES=ImgFts0V0_data,ImgFts1V0_data,ImgFts2V0_data,ImgFts3V0_data,message_fts_v4_0_data,message_fts_v4_1_data,message_fts_v4_2_data,message_fts_v4_3_data
ROLE=全文检索虚拟表的影子/内部表，不应视为独立业务实体。
COLS=0:id:INTEGER:-:PK1:NULL => 记录标识；具体作用域需结合所在表确认[N]; 1:block:BLOB:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]
G4 TABLES=ImgFts0V0_docsize,ImgFts1V0_docsize,ImgFts2V0_docsize,ImgFts3V0_docsize,message_fts_v4_0_docsize,message_fts_v4_1_docsize,message_fts_v4_2_docsize,message_fts_v4_3_docsize
ROLE=全文检索虚拟表的影子/内部表，不应视为独立业务实体。
COLS=0:id:INTEGER:-:PK1:NULL => 记录标识；具体作用域需结合所在表确认[N]; 1:sz:BLOB:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]
G5 TABLES=ImgFts0V0_idx,ImgFts1V0_idx,ImgFts2V0_idx,ImgFts3V0_idx,message_fts_v4_0_idx,message_fts_v4_1_idx,message_fts_v4_2_idx,message_fts_v4_3_idx
ROLE=全文检索虚拟表的影子/内部表，不应视为独立业务实体。
COLS=0:segid:-:NN:PK1:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 1:term:-:NN:PK2:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 2:pgno:-:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]
G6 TABLES=ImgFtsAux0V0,ImgFtsAux1V0,ImgFtsAux2V0,ImgFtsAux3V0
ROLE=全文检索相关虚拟表或范围索引；可能依赖微信私有分词器。
COLS=0:acontent:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 1:message_local_id:INTEGER:-:PK2:NULL => 关联记录 ID；仅在当前数据库的 ID 空间内有效[N]; 2:sort_seq:INTEGER:-:PK3:NULL => 消息排序序列[V]; 3:local_type:INTEGER:-:-:NULL => 当前表定义的局部记录类型；枚举含义待验证[N]; 4:session_id:INTEGER:-:PK1:NULL => 会话在当前数据库或索引中的 ID[N]; 5:sender_id:INTEGER:-:-:NULL => 发送者在当前数据库映射表中的 ID[N]; 6:create_time:INTEGER:-:-:NULL => 创建时间，通常为 Unix 时间戳[V]
G7 TABLES=ImgFtsV0
ROLE=全文检索相关虚拟表或范围索引；可能依赖微信私有分词器。
COLS=0:session_id:INTEGER:-:PK1:NULL => 会话在当前数据库或索引中的 ID[N]; 1:local_id:INTEGER:-:PK2:NULL => 本地消息或业务记录 ID[V]; 2:create_time:INTEGER:-:-:NULL => 创建时间，通常为 Unix 时间戳[V]; 3:sort_seq:INTEGER:-:PK3:NULL => 消息排序序列[V]
G8 TABLES=ImgRangeV0,message_fts_v4_range,table_info
ROLE=位于 message/message_fts.db 的业务或内部表；用途主要依据表名推测。
COLS=0:db_time_stamp:INTEGER:-:PK2:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 1:start_local_id:INTEGER:-:-:NULL => 关联记录 ID；仅在当前数据库的 ID 空间内有效[N]; 2:end_local_id:INTEGER:-:-:NULL => 关联记录 ID；仅在当前数据库的 ID 空间内有效[N]; 3:session_id:INTEGER:-:PK1:NULL => 会话在当前数据库或索引中的 ID[N]
G9 TABLES=ImgTableInfo
ROLE=位于 message/message_fts.db 的业务或内部表；用途主要依据表名推测。
COLS=0:Key:TEXT:-:PK1:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 1:ValueInt64:INTEGER:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 2:ValueDouble:REAL:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 3:ValueStdStr:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 4:ValueBlob:BLOB:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]
G10 TABLES=deleteImgFtsV0,message_fts_v4_session_delete_info
ROLE=全文检索相关虚拟表或范围索引；可能依赖微信私有分词器。
COLS=0:session_id:INTEGER:-:PK1:NULL => 会话在当前数据库或索引中的 ID[N]; 1:start_local_id:INTEGER:-:-:NULL => 关联记录 ID；仅在当前数据库的 ID 空间内有效[N]; 2:end_local_id:INTEGER:-:-:NULL => 关联记录 ID；仅在当前数据库的 ID 空间内有效[N]; 3:db_time_stamp:INTEGER:-:PK2:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]
G11 TABLES=message_fts_v4_0_content,message_fts_v4_1_content,message_fts_v4_2_content,message_fts_v4_3_content
ROLE=全文检索虚拟表的影子/内部表，不应视为独立业务实体。
COLS=0:id:INTEGER:-:PK1:NULL => 记录标识；具体作用域需结合所在表确认[N]; 1:c0:-:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 2:c1:-:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 3:c2:-:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 4:c3:-:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 5:c4:-:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 6:c5:-:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 7:c6:-:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]
G12 TABLES=message_fts_v4_aux_0,message_fts_v4_aux_1,message_fts_v4_aux_2,message_fts_v4_aux_3
ROLE=全文检索相关虚拟表或范围索引；可能依赖微信私有分词器。
COLS=0:message_local_id:INTEGER:-:PK2:NULL => 关联记录 ID；仅在当前数据库的 ID 空间内有效[N]; 1:sort_seq:INTEGER:-:PK3:NULL => 消息排序序列[V]; 2:session_id:INTEGER:-:PK1:NULL => 会话在当前数据库或索引中的 ID[N]
G13 TABLES=name2id
ROLE=名称到当前数据库内部 rowid 的映射表；不可直接跨库连接。
COLS=0:username:TEXT:-:PK1:NULL => 微信内部用户名/会话唯一标识[V]

## DB `message/message_resource.db` tables=6
ROLE: 消息与图片、文件等资源的关联索引（参考指南）。
TABLES: ChatName2Id, FtsDeleteInfo, FtsRange, MessageResourceDetail, MessageResourceInfo, SenderName2Id
G1 TABLES=ChatName2Id,SenderName2Id
ROLE=名称到当前数据库内部 rowid 的映射表；不可直接跨库连接。
COLS=0:user_name:TEXT:-:PK1:NULL => 微信内部用户名；常用于名称映射[N]; 1:update_time:INTEGER:-:-:NULL => 更新时间，通常为 Unix 时间戳[N]
G2 TABLES=FtsDeleteInfo
ROLE=全文检索相关虚拟表或范围索引；可能依赖微信私有分词器。
COLS=0:session_id:INTEGER:-:-:NULL => 会话在当前数据库或索引中的 ID[N]; 1:max_message_id:INTEGER:-:-:NULL => 关联记录 ID；仅在当前数据库的 ID 空间内有效[N]
G3 TABLES=FtsRange
ROLE=全文检索相关虚拟表或范围索引；可能依赖微信私有分词器。
COLS=0:session_id:INTEGER:-:-:NULL => 会话在当前数据库或索引中的 ID[N]; 1:db_time_stamp:INTEGER:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 2:start_local_id:INTEGER:-:-:NULL => 关联记录 ID；仅在当前数据库的 ID 空间内有效[N]; 3:end_local_id:INTEGER:-:-:NULL => 关联记录 ID；仅在当前数据库的 ID 空间内有效[N]; 4:range_type:INTEGER:-:-:NULL => 类型枚举值[N]
G4 TABLES=MessageResourceDetail
ROLE=消息资源或资源关联数据。
COLS=0:resource_id:INTEGER:-:PK1:NULL => 关联记录 ID；仅在当前数据库的 ID 空间内有效[N]; 1:message_id:INTEGER:-:-:NULL => 消息标识[N]; 2:type:INTEGER:-:-:NULL => 类型枚举值[N]; 3:size:INTEGER:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 4:create_time:INTEGER:-:-:NULL => 创建时间，通常为 Unix 时间戳[V]; 5:access_time:INTEGER:-:-:NULL => 时间值；单位和时区需结合样本验证[N]; 6:status:INTEGER:-:-:NULL => 状态枚举值[N]; 7:data_index:TEXT:-:-:NULL => 二进制或序列化数据，编码需进一步确认[N]; 8:packed_info:BLOB:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]
G5 TABLES=MessageResourceInfo
ROLE=消息资源或资源关联数据。
COLS=0:message_id:INTEGER:-:PK1:NULL => 消息标识[N]; 1:chat_id:INTEGER:-:-:NULL => 会话在当前数据库映射表中的 ID[N]; 2:sender_id:INTEGER:-:-:NULL => 发送者在当前数据库映射表中的 ID[N]; 3:message_local_type:INTEGER:-:-:NULL => 类型枚举值[N]; 4:message_create_time:INTEGER:-:-:NULL => 时间值；单位和时区需结合样本验证[N]; 5:message_local_id:INTEGER:-:-:NULL => 关联记录 ID；仅在当前数据库的 ID 空间内有效[N]; 6:message_svr_id:INTEGER:-:-:NULL => 关联记录 ID；仅在当前数据库的 ID 空间内有效[N]; 7:message_origin_source:INTEGER:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 8:packed_info:BLOB:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]

## DB `message/weclaw.db` tables=0
ROLE: 当前快照为空；用途待验证。
TABLES: -

## DB `session/session.db` tables=7
ROLE: 会话摘要、未读、草稿和删除状态（参考指南）。
TABLES: Name2Id, SessionDeleteTable, SessionDraft, SessionNoContactInfoTable, SessionTable, SessionUnreadListTable_1, SessionUnreadStatTable_1
G1 TABLES=Name2Id
ROLE=名称到当前数据库内部 rowid 的映射表；不可直接跨库连接。
COLS=0:user_name:TEXT:-:PK1:NULL => 微信内部用户名；常用于名称映射[N]
G2 TABLES=SessionDeleteTable
ROLE=删除记录或删除状态。
COLS=0:username:TEXT:-:PK1:NULL => 微信内部用户名/会话唯一标识[V]; 1:delete_time:INTEGER:-:-:NULL => 时间值；单位和时区需结合样本验证[N]
G3 TABLES=SessionDraft
ROLE=会话草稿数据。
COLS=0:username:TEXT:-:-:NULL => 微信内部用户名/会话唯一标识[V]; 1:window_id:TEXT:-:-:NULL => 关联记录 ID；仅在当前数据库的 ID 空间内有效[N]; 2:timestamp:INTEGER:-:-:NULL => 时间值；单位和时区需结合样本验证[N]; 3:draft_data:BLOB:-:-:NULL => 二进制或序列化数据，编码需进一步确认[N]
G4 TABLES=SessionNoContactInfoTable
ROLE=位于 session/session.db 的业务或内部表；用途主要依据表名推测。
COLS=0:username:TEXT:-:PK1:NULL => 微信内部用户名/会话唯一标识[V]; 1:session_title:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]
G5 TABLES=SessionTable
ROLE=会话摘要与排序状态主表，不保存完整消息正文。
COLS=0:username:TEXT:-:PK1:NULL => 微信内部用户名/会话唯一标识[V]; 1:type:INTEGER:-:-:NULL => 类型枚举值[N]; 2:unread_count:INTEGER:-:-:NULL => 未读数量[N]; 3:unread_first_msg_srv_id:INTEGER:-:-:NULL => 关联记录 ID；仅在当前数据库的 ID 空间内有效[N]; 4:unread_first_pat_msg_local_id:INTEGER:-:-:NULL => 关联记录 ID；仅在当前数据库的 ID 空间内有效[N]; 5:unread_first_pat_msg_sort_seq:INTEGER:-:-:NULL => 排序或递增序列[N]; 6:is_hidden:INTEGER:-:-:NULL => 布尔或状态标记[N]; 7:summary:TEXT:-:-:NULL => 会话最后消息摘要，不等同于完整正文[V]; 8:draft:TEXT:-:-:NULL => 会话草稿内容[N]; 9:status:INTEGER:-:-:NULL => 状态枚举值[N]; 10:last_timestamp:INTEGER:-:-:NULL => 时间值；单位和时区需结合样本验证[N]; 11:sort_timestamp:INTEGER:-:-:NULL => 时间值；单位和时区需结合样本验证[N]; 12:last_clear_unread_timestamp:INTEGER:-:-:NULL => 时间值；单位和时区需结合样本验证[N]; 13:last_msg_locald_id:INTEGER:-:-:NULL => 关联记录 ID；仅在当前数据库的 ID 空间内有效[N]; 14:last_msg_type:INTEGER:-:-:NULL => 类型枚举值[N]; 15:last_msg_sub_type:INTEGER:-:-:NULL => 类型枚举值[N]; 16:last_msg_sender:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 17:last_sender_display_name:TEXT:-:-:NULL => 名称或显示名称[N]; 18:last_msg_ext_type:INTEGER:-:-:NULL => 类型枚举值[N]
G6 TABLES=SessionUnreadListTable_1
ROLE=会话未读状态或统计。
COLS=0:username_id:INTEGER:-:PK1:NULL => username 在当前数据库 Name2Id 表中的 rowid[N]; 1:server_id:INTEGER:-:PK2:NULL => 服务端消息 ID[V]; 2:create_time:INTEGER:-:-:NULL => 创建时间，通常为 Unix 时间戳[V]
G7 TABLES=SessionUnreadStatTable_1
ROLE=会话未读状态或统计。
COLS=0:username_id:INTEGER:-:PK1:NULL => username 在当前数据库 Name2Id 表中的 rowid[N]; 1:unread_stat:INTEGER:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]

## DB `sns/sns.db` tables=12
ROLE: 朋友圈/SNS 内容及索引（名称推测）。
TABLES: SnsAdTimeLine, SnsDraft, SnsErrorMessage, SnsIgnoredDataItem, SnsMainTimeLineBreakFlag, SnsMessage_tmp3, SnsNoteVoice, SnsPendingDraftDeletionTable, SnsPublishTask, SnsTimeLine, SnsTopItem_1, SnsUserTimeLineBreakFlagV2
G1 TABLES=SnsAdTimeLine
ROLE=位于 sns/sns.db 的业务或内部表；用途主要依据表名推测。
COLS=0:tid:INTEGER:-:PK1:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 1:username:TEXT:-:-:NULL => 微信内部用户名/会话唯一标识[V]; 2:content:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 3:create_time:INTEGER:-:-:NULL => 创建时间，通常为 Unix 时间戳[V]; 4:ad_content:TEXT:-:-:NULL => 正文或内容数据[N]; 5:ad_create_time:INTEGER:-:-:NULL => 时间值；单位和时区需结合样本验证[N]; 6:exposure_time:INTEGER:-:-:NULL => 时间值；单位和时区需结合样本验证[N]; 7:exposure_count:INTEGER:-:-:NULL => 数量或计数[N]; 8:remind_source_info:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 9:remind_self_info:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 10:extra_data:TEXT:-:-:NULL => 二进制或序列化数据，编码需进一步确认[N]
G2 TABLES=SnsDraft,SnsPublishTask
ROLE=会话草稿数据。
COLS=0:local_id:INTEGER:-:PK1:NULL => 本地消息或业务记录 ID[V]; 1:create_time:INTEGER:-:-:NULL => 创建时间，通常为 Unix 时间戳[V]; 2:ui_type:INTEGER:-:-:NULL => 类型枚举值[N]; 3:content:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 4:client_id:TEXT:-:-:NULL => 关联记录 ID；仅在当前数据库的 ID 空间内有效[N]; 5:status:INTEGER:-:-:NULL => 状态枚举值[N]
G3 TABLES=SnsErrorMessage
ROLE=位于 sns/sns.db 的业务或内部表；用途主要依据表名推测。
COLS=0:local_id:INTEGER:-:PK1:NULL => 本地消息或业务记录 ID[V]; 1:error_type:INTEGER:-:-:NULL => 类型枚举值[N]; 2:creat_time:INTEGER:-:-:NULL => 时间值；单位和时区需结合样本验证[N]; 3:tid:INTEGER:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 4:packed_info_data:TEXT:-:-:NULL => 未解析的打包扩展数据[V]
G4 TABLES=SnsIgnoredDataItem
ROLE=位于 sns/sns.db 的业务或内部表；用途主要依据表名推测。
COLS=0:tid:INTEGER:-:PK1:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]
G5 TABLES=SnsMainTimeLineBreakFlag
ROLE=位于 sns/sns.db 的业务或内部表；用途主要依据表名推测。
COLS=0:tid:INTEGER:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 1:tid_heigh_bit:INTEGER:-:PK1:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 2:tid_low_bit:INTEGER:-:PK2:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 3:break_flag:INTEGER:-:-:NULL => 位标志集合[N]
G6 TABLES=SnsMessage_tmp3
ROLE=位于 sns/sns.db 的业务或内部表；用途主要依据表名推测。
COLS=0:local_id:INTEGER:-:PK1:NULL => 本地消息或业务记录 ID[V]; 1:create_time:INTEGER:-:-:NULL => 创建时间，通常为 Unix 时间戳[V]; 2:type:INTEGER:-:-:NULL => 类型枚举值[N]; 3:feed_id:INTEGER:-:-:NULL => 关联记录 ID；仅在当前数据库的 ID 空间内有效[N]; 4:is_unread:INTEGER:-:-:NULL => 布尔或状态标记[N]; 5:from_username:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 6:from_nickname:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 7:to_username:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 8:to_nickname:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 9:content:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 10:serialized_comment_buf:BLOB:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 11:serialized_ref_buf:BLOB:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 12:comment_id:INTEGER:-:-:NULL => 关联记录 ID；仅在当前数据库的 ID 空间内有效[N]; 13:client_id:TEXT:-:-:NULL => 关联记录 ID；仅在当前数据库的 ID 空间内有效[N]; 14:comment64_id:INTEGER:-:-:NULL => 关联记录 ID；仅在当前数据库的 ID 空间内有效[N]; 15:comment_flag:INTEGER:-:-:NULL => 位标志集合[N]; 16:del_status:INTEGER:-:-:NULL => 状态枚举值[N]; 17:is_relative_me:INTEGER:-:-:NULL => 布尔或状态标记[N]
G7 TABLES=SnsNoteVoice
ROLE=位于 sns/sns.db 的业务或内部表；用途主要依据表名推测。
COLS=0:tid:INTEGER:-:PK1:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 1:data_id:TEXT:-:PK2:NULL => 关联记录 ID；仅在当前数据库的 ID 空间内有效[N]; 2:buff:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]
G8 TABLES=SnsPendingDraftDeletionTable
ROLE=会话草稿数据。
COLS=0:local_id:INTEGER:-:PK1:NULL => 本地消息或业务记录 ID[V]; 1:create_time:INTEGER:-:-:NULL => 创建时间，通常为 Unix 时间戳[V]; 2:ui_type:INTEGER:-:-:NULL => 类型枚举值[N]; 3:content:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 4:client_id:TEXT:-:-:NULL => 关联记录 ID；仅在当前数据库的 ID 空间内有效[N]; 5:status:INTEGER:-:-:NULL => 状态枚举值[N]; 6:tid:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]
G9 TABLES=SnsTimeLine
ROLE=位于 sns/sns.db 的业务或内部表；用途主要依据表名推测。
COLS=0:tid:INTEGER:-:PK1:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 1:user_name:TEXT:-:-:NULL => 微信内部用户名；常用于名称映射[N]; 2:content:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 3:pack_info_buf:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]
G10 TABLES=SnsTopItem_1
ROLE=位于 sns/sns.db 的业务或内部表；用途主要依据表名推测。
COLS=0:tid:INTEGER:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 1:username:TEXT:-:-:NULL => 微信内部用户名/会话唯一标识[V]; 2:summary:TEXT:-:-:NULL => 会话最后消息摘要，不等同于完整正文[V]; 3:create_time:INTEGER:-:-:NULL => 创建时间，通常为 Unix 时间戳[V]; 4:last_read_time:INTEGER:-:-:NULL => 时间值；单位和时区需结合样本验证[N]; 5:is_read:INTEGER:-:-:NULL => 布尔或状态标记[N]
G11 TABLES=SnsUserTimeLineBreakFlagV2
ROLE=位于 sns/sns.db 的业务或内部表；用途主要依据表名推测。
COLS=0:tid:INTEGER:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 1:tid_heigh_bit:INTEGER:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 2:tid_low_bit:INTEGER:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 3:break_flag:INTEGER:-:-:NULL => 位标志集合[N]; 4:user_name:TEXT:-:-:NULL => 微信内部用户名；常用于名称映射[N]

## DB `solitaire/solitaire.db` tables=3
ROLE: 群接龙数据（参考指南）。
TABLES: SolitaireFold_972c83c3bf7b2c97e19e6e4c14883575, SolitaireValid_972c83c3bf7b2c97e19e6e4c14883575, Solitaire_972c83c3bf7b2c97e19e6e4c14883575
G1 TABLES=SolitaireFold_972c83c3bf7b2c97e19e6e4c14883575
ROLE=群接龙相关数据。
COLS=0:front_content:TEXT:-:-:NULL => 正文或内容数据[N]; 1:behind_content:TEXT:-:-:NULL => 正文或内容数据[N]; 2:local_id:INTEGER:-:-:NULL => 本地消息或业务记录 ID[V]; 3:sort_seq:INTEGER:-:-:NULL => 消息排序序列[V]; 4:create_time:INTEGER:-:-:NULL => 创建时间，通常为 Unix 时间戳[V]
G2 TABLES=SolitaireValid_972c83c3bf7b2c97e19e6e4c14883575
ROLE=群接龙相关数据。
COLS=0:title:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 1:valid:INTEGER:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 2:local_id:INTEGER:-:-:NULL => 本地消息或业务记录 ID[V]; 3:sort_seq:INTEGER:-:-:NULL => 消息排序序列[V]; 4:create_time:INTEGER:-:-:NULL => 创建时间，通常为 Unix 时间戳[V]
G3 TABLES=Solitaire_972c83c3bf7b2c97e19e6e4c14883575
ROLE=群接龙相关数据。
COLS=0:header:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 1:footer:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 2:content:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 3:solitaire_string:TEXT:-:-:NULL => 用途未知，需结合样本值、调用代码或逆向结果验证[U]; 4:active_time:INTEGER:-:-:NULL => 时间值；单位和时区需结合样本验证[N]
