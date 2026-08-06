#!/usr/bin/env python3
"""Export an encrypted Weixin WCDB snapshot to ordinary SQLite.

The source database is opened read-only. The destination contains the original
schema and stored values, but no SQLCipher page encryption. WCDB-compressed
columns remain stored as BLOBs because compression is above SQLite's page
format.
"""

from __future__ import annotations

import argparse
import json
import sqlite3
from pathlib import Path
from typing import Any

from verify_wcdb_offline import open_wcdb_readonly


SQLITE_HEADER = b"SQLite format 3\x00"


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Export a copied Weixin SQLCipher/WCDB database to a standard "
            "unencrypted SQLite file."
        )
    )
    parser.add_argument("database", type=Path, help="copied message_0.db")
    parser.add_argument("output", type=Path, help="new plain SQLite file")
    parser.add_argument(
        "--key-record",
        type=Path,
        default=Path(
            "artifacts/exports/wcdb_cipher_key_message_0.json"
        ),
        help="captured WCDB cipher-key JSON",
    )
    return parser


def quote_identifier(identifier: str) -> str:
    return '"' + identifier.replace('"', '""') + '"'


def verify_plain_database(database: Path) -> dict[str, Any]:
    with database.open("rb") as source:
        header = source.read(len(SQLITE_HEADER))
    if header != SQLITE_HEADER:
        raise RuntimeError("destination does not have a standard SQLite header")

    uri = database.resolve().as_uri() + "?mode=ro"
    connection = sqlite3.connect(uri, uri=True)
    try:
        quick_check = connection.execute(
            "PRAGMA quick_check"
        ).fetchone()[0]
        if quick_check != "ok":
            raise RuntimeError(f"plain SQLite quick_check failed: {quick_check}")

        object_counts = {
            object_type: int(count)
            for object_type, count in connection.execute(
                "SELECT type, COUNT(*) FROM sqlite_master GROUP BY type"
            )
        }
        message_tables = [
            row[0]
            for row in connection.execute(
                "SELECT name FROM sqlite_master "
                "WHERE type = 'table' AND name LIKE 'Msg_%' "
                "ORDER BY name"
            )
        ]
        message_rows = sum(
            int(
                connection.execute(
                    f"SELECT COUNT(*) FROM {quote_identifier(table)}"
                ).fetchone()[0]
            )
            for table in message_tables
        )
    finally:
        connection.close()

    return {
        "sqlite_header": "SQLite format 3",
        "quick_check": quick_check,
        "tables": object_counts.get("table", 0),
        "indexes": object_counts.get("index", 0),
        "views": object_counts.get("view", 0),
        "triggers": object_counts.get("trigger", 0),
        "message_tables": len(message_tables),
        "message_rows": message_rows,
    }


def export_plain_database(
    source: Path, output: Path, key_record: Path
) -> dict[str, Any]:
    source = source.resolve()
    output = output.resolve()
    partial = output.with_suffix(output.suffix + ".partial")

    if output.exists():
        raise FileExistsError(f"refusing to overwrite destination: {output}")
    if partial.exists():
        raise FileExistsError(
            f"partial export already exists; inspect or remove it: {partial}"
        )
    if source == output or source == partial:
        raise ValueError("source and destination must be different files")

    output.parent.mkdir(parents=True, exist_ok=True)
    connection = open_wcdb_readonly(source, key_record)
    attached = False
    try:
        connection.execute(
            "ATTACH DATABASE ? AS plaintext KEY ''", (str(partial),)
        )
        attached = True
        connection.execute("PRAGMA plaintext.journal_mode = DELETE")
        connection.execute("SELECT sqlcipher_export('plaintext')")
        connection.commit()
        connection.execute("DETACH DATABASE plaintext")
        attached = False
    finally:
        if attached:
            try:
                connection.execute("DETACH DATABASE plaintext")
            except Exception:
                pass
        connection.close()

    verification = verify_plain_database(partial)
    partial.rename(output)
    verification.update(
        {
            "source": str(source),
            "output": str(output),
            "output_bytes": output.stat().st_size,
            "encrypted": False,
        }
    )
    return verification


def main() -> int:
    args = build_parser().parse_args()
    result = export_plain_database(
        args.database, args.output, args.key_record
    )
    print(json.dumps(result, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

