#!/usr/bin/env python3
"""Verify a copied Weixin WCDB database using a captured cipher-key record.

The source database is opened read-only. Neither the captured key nor the
derived SQLCipher page key is printed.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from sqlcipher3 import dbapi2 as sqlite


SQLCIPHER4_KDF_ITERATIONS = 256_000
SQLCIPHER_KEY_BYTES = 32
SQLCIPHER_SALT_BYTES = 16


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Open and verify a copied Weixin WCDB database read-only."
    )
    parser.add_argument("database", type=Path, help="copied WCDB database")
    parser.add_argument(
        "--key-record",
        type=Path,
        default=Path(
            "artifacts/exports/wcdb_cipher_key_message_0.json"
        ),
        help="JSON produced by tools/debugger/extract_wcdb_cipher_key.py",
    )
    parser.add_argument(
        "--list-tables",
        action="store_true",
        help="include table names in the JSON result",
    )
    return parser


def load_captured_key(path: Path) -> tuple[bytes, int]:
    record = json.loads(path.read_text(encoding="utf-8"))
    captured_key = bytes.fromhex(record["key_hex"])
    if len(captured_key) != SQLCIPHER_KEY_BYTES:
        raise ValueError(
            f"expected a {SQLCIPHER_KEY_BYTES}-byte captured key, "
            f"got {len(captured_key)} bytes"
        )

    compatibility = int(record["cipher_compatibility"])
    if compatibility != 4:
        raise ValueError(
            f"this verifier currently supports SQLCipher compatibility 4, "
            f"got {compatibility}"
        )
    return captured_key, compatibility


def derive_sqlcipher4_key(database: Path, captured_key: bytes) -> bytes:
    with database.open("rb") as source:
        salt = source.read(SQLCIPHER_SALT_BYTES)
    if len(salt) != SQLCIPHER_SALT_BYTES:
        raise ValueError("database is too short to contain a SQLCipher salt")

    # WCDB passes the captured binary Data to sqlite3_key(). SQLCipher 4 first
    # derives the AES page key from that input and the per-database page-1 salt.
    return hashlib.pbkdf2_hmac(
        "sha512",
        captured_key,
        salt,
        SQLCIPHER4_KDF_ITERATIONS,
        dklen=SQLCIPHER_KEY_BYTES,
    )


def open_wcdb_readonly(
    database: Path, key_record: Path
) -> sqlite.Connection:
    database = database.resolve()
    captured_key, compatibility = load_captured_key(key_record)
    page_key = derive_sqlcipher4_key(database, captured_key)

    connection = sqlite.connect(database.as_uri() + "?mode=ro", uri=True)
    try:
        connection.execute(f'''PRAGMA key = "x'{page_key.hex()}'"''')
        connection.execute(
            f"PRAGMA cipher_compatibility = {compatibility}"
        )
    except Exception:
        connection.close()
        raise
    return connection


def main() -> int:
    args = build_parser().parse_args()
    database = args.database.resolve()
    connection = open_wcdb_readonly(database, args.key_record)
    try:
        cipher_version = connection.execute(
            "PRAGMA cipher_version"
        ).fetchone()[0]
        tables = [
            row[0]
            for row in connection.execute(
                "SELECT name FROM sqlite_master "
                "WHERE type = 'table' ORDER BY name"
            )
        ]
        quick_check = connection.execute("PRAGMA quick_check").fetchone()[0]
    finally:
        connection.close()

    result: dict[str, object] = {
        "opened": True,
        "readonly": True,
        "cipher_version": cipher_version,
        "table_count": len(tables),
        "quick_check": quick_check,
    }
    if args.list_tables:
        result["tables"] = tables
    print(json.dumps(result, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
