#!/usr/bin/env python3
"""Export all message shards from a copied Weixin WCDB database to JSONL."""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
from datetime import datetime
from pathlib import Path
from typing import Any, Iterable
from zoneinfo import ZoneInfo

import zstandard as zstd

from verify_wcdb_offline import open_wcdb_readonly


MESSAGE_COLUMNS = (
    "local_id",
    "server_id",
    "local_type",
    "sort_seq",
    "real_sender_id",
    "create_time",
    "status",
    "upload_status",
    "download_status",
    "server_seq",
    "origin_source",
    "source",
    "message_content",
    "compress_content",
    "packed_info_data",
    "WCDB_CT_message_content",
    "WCDB_CT_source",
)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Read a copied Weixin message_0.db and stream its message shards "
            "to one chronological JSONL file."
        )
    )
    parser.add_argument("database", type=Path, help="copied message_0.db")
    parser.add_argument("output", type=Path, help="destination JSONL file")
    parser.add_argument(
        "--key-record",
        type=Path,
        default=Path(
            "artifacts/exports/wcdb_cipher_key_message_0.json"
        ),
        help="JSON produced by tools/debugger/extract_wcdb_cipher_key.py",
    )
    parser.add_argument(
        "--conversation",
        action="append",
        dest="conversations",
        help="export only this user_name; may be specified more than once",
    )
    parser.add_argument(
        "--timezone",
        default="Asia/Shanghai",
        help="IANA timezone used for create_time_iso",
    )
    parser.add_argument(
        "--include-packed-info",
        action="store_true",
        help="include packed_info_data as base64",
    )
    parser.add_argument(
        "--limit",
        type=int,
        help="maximum rows after global chronological ordering",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="overwrite an existing output file",
    )
    return parser


def quote_identifier(identifier: str) -> str:
    return '"' + identifier.replace('"', '""') + '"'


def table_for_user_name(user_name: str) -> str:
    digest = hashlib.md5(user_name.encode("utf-8")).hexdigest()
    return f"Msg_{digest}"


def decode_wcdb_text(
    value: Any,
    compression_flag: int,
    decompressor: zstd.ZstdDecompressor,
) -> tuple[str | None, bool]:
    if value is None:
        return None, False

    was_compressed = compression_flag == 4
    if was_compressed:
        if not isinstance(value, bytes):
            raise TypeError(
                "WCDB compression flag 4 was set for a non-BLOB value"
            )
        value = decompressor.decompress(value)

    if isinstance(value, bytes):
        return value.decode("utf-8"), was_compressed
    if isinstance(value, str):
        return value, was_compressed
    return str(value), was_compressed


def iso_timestamp(value: int, timezone: ZoneInfo) -> str | None:
    if value <= 0:
        return None
    return datetime.fromtimestamp(value, timezone).isoformat()


def select_conversations(
    connection: Any, requested: list[str] | None
) -> list[tuple[str, int, str]]:
    existing_tables = {
        row[0]
        for row in connection.execute(
            "SELECT name FROM sqlite_master "
            "WHERE type = 'table' AND name LIKE 'Msg_%'"
        )
    }
    requested_set = set(requested) if requested else None
    conversations: list[tuple[str, int, str]] = []

    for user_name, is_session in connection.execute(
        "SELECT user_name, is_session FROM Name2Id WHERE is_session != 0 "
        "ORDER BY user_name"
    ):
        if requested_set is not None and user_name not in requested_set:
            continue
        table = table_for_user_name(user_name)
        if table not in existing_tables:
            raise RuntimeError(
                f"Name2Id maps {user_name!r} to missing table {table}"
            )
        conversations.append((user_name, int(is_session), table))

    if requested_set is not None:
        found = {row[0] for row in conversations}
        missing = sorted(requested_set - found)
        if missing:
            raise ValueError(
                "requested conversation(s) not found: " + ", ".join(missing)
            )

    mapped_tables = {row[2] for row in conversations}
    if requested_set is None and mapped_tables != existing_tables:
        raise RuntimeError(
            "Name2Id/Msg_* mapping is incomplete: "
            f"{len(mapped_tables)} mapped vs {len(existing_tables)} tables"
        )
    return conversations


def build_message_query(
    conversations: Iterable[tuple[str, int, str]],
    limit: int | None,
) -> tuple[str, list[Any]]:
    selects: list[str] = []
    parameters: list[Any] = []
    columns = ", ".join(quote_identifier(name) for name in MESSAGE_COLUMNS)

    for user_name, is_session, table in conversations:
        selects.append(
            f"SELECT ? AS conversation_username, "
            f"? AS conversation_is_session, {columns} "
            f"FROM {quote_identifier(table)}"
        )
        parameters.extend((user_name, is_session))

    if not selects:
        raise ValueError("no conversations selected")

    query = " UNION ALL ".join(selects)
    query += " ORDER BY sort_seq, local_id"
    if limit is not None:
        if limit <= 0:
            raise ValueError("--limit must be positive")
        query += " LIMIT ?"
        parameters.append(limit)
    return query, parameters


def main() -> int:
    args = build_parser().parse_args()
    if args.output.exists() and not args.force:
        raise FileExistsError(
            f"output already exists: {args.output}; use --force to overwrite"
        )

    timezone = ZoneInfo(args.timezone)
    connection = open_wcdb_readonly(args.database, args.key_record)
    decompressor = zstd.ZstdDecompressor()
    written = 0
    compressed_message_content = 0
    compressed_source = 0

    try:
        connection.execute("PRAGMA temp_store = MEMORY")
        conversations = select_conversations(
            connection, args.conversations
        )
        sender_names = {
            int(rowid): user_name
            for rowid, user_name in connection.execute(
                "SELECT rowid, user_name FROM Name2Id"
            )
        }
        query, parameters = build_message_query(
            conversations, args.limit
        )

        args.output.parent.mkdir(parents=True, exist_ok=True)
        with args.output.open("w", encoding="utf-8", newline="\n") as output:
            for row in connection.execute(query, parameters):
                conversation_username = row[0]
                conversation_is_session = bool(row[1])
                values = dict(zip(MESSAGE_COLUMNS, row[2:], strict=True))

                message_content, message_was_compressed = decode_wcdb_text(
                    values["message_content"],
                    int(values["WCDB_CT_message_content"]),
                    decompressor,
                )
                source, source_was_compressed = decode_wcdb_text(
                    values["source"],
                    int(values["WCDB_CT_source"]),
                    decompressor,
                )
                compressed_message_content += int(message_was_compressed)
                compressed_source += int(source_was_compressed)

                local_type = int(values["local_type"])
                origin_source = int(values["origin_source"])
                record: dict[str, Any] = {
                    "conversation_username": conversation_username,
                    "conversation_is_session": conversation_is_session,
                    "local_id": values["local_id"],
                    "server_id": values["server_id"],
                    "local_type": local_type,
                    "local_type_base": local_type & 0xFFFFFFFF,
                    "local_type_flags": local_type >> 32,
                    "sort_seq": values["sort_seq"],
                    "real_sender_id": values["real_sender_id"],
                    "real_sender_username": sender_names[
                        int(values["real_sender_id"])
                    ],
                    "create_time": values["create_time"],
                    "create_time_iso": iso_timestamp(
                        int(values["create_time"]), timezone
                    ),
                    "status": values["status"],
                    "upload_status": values["upload_status"],
                    "download_status": values["download_status"],
                    "server_seq": values["server_seq"],
                    "origin_source": origin_source,
                    "origin_source_hex": (
                        f"{origin_source & ((1 << 64) - 1):016x}"
                    ),
                    "source": source,
                    "message_content": message_content,
                    "compress_content": values["compress_content"],
                    "message_content_was_compressed": (
                        message_was_compressed
                    ),
                    "source_was_compressed": source_was_compressed,
                }
                if args.include_packed_info:
                    packed = values["packed_info_data"]
                    record["packed_info_data_b64"] = (
                        base64.b64encode(packed).decode("ascii")
                        if packed is not None
                        else None
                    )

                output.write(
                    json.dumps(record, ensure_ascii=False, separators=(",", ":"))
                )
                output.write("\n")
                written += 1
    finally:
        connection.close()

    print(
        json.dumps(
            {
                "output": str(args.output.resolve()),
                "rows": written,
                "conversations": len(conversations),
                "compressed_message_content_decoded": (
                    compressed_message_content
                ),
                "compressed_source_decoded": compressed_source,
            },
            ensure_ascii=False,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
