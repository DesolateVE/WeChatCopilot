#!/usr/bin/env python3
"""Read a WCDB cipher key at Weixin's SetCipherKey breakpoint.

The script is read-only with respect to the debuggee and databases. It writes
the recovered key to a separate JSON file and prints only metadata plus a
SHA-256 fingerprint to the console.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from x64dbg_automate import X64DbgClient


DEFAULT_SET_CIPHER_KEY = 0x7FF9065FBF40
MAX_KEY_BYTES = 4096


def parse_int(value: str) -> int:
    return int(value, 0)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Extract a WCDB cipher key from a paused Weixin process."
    )
    parser.add_argument("--session-pid", type=int, required=True)
    parser.add_argument(
        "--x64dbg-path",
        default=r"D:\Scoop\apps\x64dbg\current\release\x64\x64dbg.exe",
    )
    parser.add_argument(
        "--breakpoint-address",
        type=parse_int,
        default=DEFAULT_SET_CIPHER_KEY,
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("artifacts/exports/wcdb_cipher_key.json"),
    )
    return parser


def main() -> int:
    args = build_parser().parse_args()
    client = X64DbgClient(args.x64dbg_path)
    client.attach_session(args.session_pid)
    try:
        if not client.is_debugging():
            raise RuntimeError("the selected x64dbg session has no debuggee")
        if client.is_running():
            raise RuntimeError(
                "debuggee is running; pause at WCDB SetCipherKey first"
            )

        rip = client.get_reg("rip")
        if rip != args.breakpoint_address:
            raise RuntimeError(
                f"unexpected RIP 0x{rip:X}; expected "
                f"0x{args.breakpoint_address:X}"
            )

        # WCDB Data layout confirmed statically:
        #   +0x08 bytes pointer
        #   +0x10 byte length
        data_object = client.get_reg("rdx")
        key_pointer = client.read_qword(data_object + 0x08)
        key_length = client.read_qword(data_object + 0x10)
        if key_length <= 0 or key_length > MAX_KEY_BYTES:
            raise ValueError(f"implausible WCDB key length: {key_length}")
        if key_pointer == 0:
            raise ValueError("WCDB key pointer is null")

        key = client.read_memory(key_pointer, key_length)
        if len(key) != key_length:
            raise ValueError(
                f"short key read: {len(key)} != expected {key_length}"
            )

        record = {
            "breakpoint": f"0x{rip:X}",
            "data_object": f"0x{data_object:X}",
            "key_pointer": f"0x{key_pointer:X}",
            "key_length": key_length,
            "key_hex": key.hex(),
            "key_sha256": hashlib.sha256(key).hexdigest(),
            "page_size": client.get_reg("r8") & 0xFFFFFFFF,
            "cipher_compatibility": client.get_reg("r9") & 0xFFFFFFFF,
        }

        args.output.parent.mkdir(parents=True, exist_ok=True)
        with args.output.open("w", encoding="utf-8", newline="\n") as output:
            json.dump(record, output, ensure_ascii=False, indent=2)
            output.write("\n")

        print(
            json.dumps(
                {
                    "key_length": record["key_length"],
                    "key_sha256": record["key_sha256"],
                    "page_size": record["page_size"],
                    "cipher_compatibility": record["cipher_compatibility"],
                    "output": str(args.output.resolve()),
                },
                ensure_ascii=False,
            )
        )
        return 0
    finally:
        client.detach_session()


if __name__ == "__main__":
    raise SystemExit(main())
