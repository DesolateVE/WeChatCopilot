#!/usr/bin/env python3
"""Read a Weixin ChatMessage vector from an existing paused x64dbg session.

This utility is deliberately read-only with respect to the debuggee and Weixin
databases. It only calls x64dbg-automate register/evaluation/memory-read APIs and
writes decoded records to a separate JSONL file.
"""

from __future__ import annotations

import argparse
import json
import struct
from datetime import datetime
from pathlib import Path
from typing import Any

from x64dbg_automate import X64DbgClient


CHAT_MESSAGE_SIZE = 0x2E0
MSVC_STRING_SIZE = 0x20
MSVC_STRING_SSO_CAPACITY = 0x0F
MAX_STRING_BYTES = 64 * 1024 * 1024


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def u64(data: bytes, offset: int) -> int:
    return struct.unpack_from("<Q", data, offset)[0]


def parse_int(value: str) -> int:
    return int(value, 0)


def read_msvc_string(
    client: X64DbgClient,
    owner: bytes,
    offset: int,
) -> str:
    storage = owner[offset : offset + MSVC_STRING_SIZE]
    if len(storage) != MSVC_STRING_SIZE:
        raise ValueError(f"truncated std::string at ChatMessage+0x{offset:X}")

    length = u64(storage, 0x10)
    capacity = u64(storage, 0x18)
    if length > MAX_STRING_BYTES:
        raise ValueError(
            f"implausible string length {length} at ChatMessage+0x{offset:X}"
        )
    if length == 0:
        return ""

    if capacity <= MSVC_STRING_SSO_CAPACITY:
        raw = storage[:length]
    else:
        pointer = u64(storage, 0)
        if pointer == 0:
            raise ValueError(
                f"null string pointer at ChatMessage+0x{offset:X}, length={length}"
            )
        raw = client.read_memory(pointer, length)
    return raw.decode("utf-8", errors="replace")


def decode_message(
    client: X64DbgClient,
    address: int,
    index: int,
) -> dict[str, Any]:
    data = client.read_memory(address, CHAT_MESSAGE_SIZE)
    if len(data) != CHAT_MESSAGE_SIZE:
        raise ValueError(
            f"short ChatMessage read at 0x{address:X}: "
            f"{len(data)} != {CHAT_MESSAGE_SIZE}"
        )

    create_time = u32(data, 0x164)
    create_time_iso = (
        datetime.fromtimestamp(create_time).astimezone().isoformat()
        if create_time
        else None
    )

    return {
        "vector_index": index,
        "debuggee_address": f"0x{address:X}",
        # These three pre-ORM strings still need cross-checking with incoming
        # single-chat and group-chat messages before assigning stricter names.
        "identity_0x18": read_msvc_string(client, data, 0x18),
        "session_username_0x38": read_msvc_string(client, data, 0x38),
        "identity_0x58": read_msvc_string(client, data, 0x58),
        "local_id": u32(data, 0x144),
        "server_id": u64(data, 0x148),
        "sort_seq": u64(data, 0x150),
        "local_type": u32(data, 0x158),
        "real_sender_id": u32(data, 0x160),
        "create_time": create_time,
        "create_time_iso": create_time_iso,
        "status": u32(data, 0x168),
        "upload_status": u32(data, 0x16C),
        "download_status": u32(data, 0x170),
        "server_seq": u32(data, 0x174),
        "origin_source_raw": data[0x178:0x180].hex(),
        "message_content": read_msvc_string(client, data, 0x180),
        "compress_content": read_msvc_string(client, data, 0x1A0),
        "source": read_msvc_string(client, data, 0x1C0),
    }


def resolve_vector_address(
    client: X64DbgClient,
    explicit_address: int | None,
    register: str | None,
) -> int:
    if explicit_address is not None:
        return explicit_address
    if not register:
        raise ValueError("provide --vector-address or --vector-register")
    value, success = client.eval_sync(register)
    if not success:
        raise ValueError(f"x64dbg could not evaluate register/expression: {register}")
    return value


def validate_vector(
    client: X64DbgClient,
    vector_address: int,
    max_messages: int,
) -> tuple[int, int]:
    begin = client.read_qword(vector_address)
    end = client.read_qword(vector_address + 8)
    capacity_end = client.read_qword(vector_address + 16)

    if begin == 0 and end == 0:
        return 0, 0
    if begin == 0 or end < begin or capacity_end < end:
        raise ValueError(
            "invalid vector bounds: "
            f"begin=0x{begin:X}, end=0x{end:X}, cap=0x{capacity_end:X}"
        )

    byte_length = end - begin
    if byte_length % CHAT_MESSAGE_SIZE:
        raise ValueError(
            f"vector byte length 0x{byte_length:X} is not divisible by "
            f"ChatMessage size 0x{CHAT_MESSAGE_SIZE:X}"
        )

    count = byte_length // CHAT_MESSAGE_SIZE
    if count > max_messages:
        raise ValueError(f"vector count {count} exceeds --max-messages")
    return begin, count


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Export a paused Weixin std::vector<ChatMessage> to JSONL."
    )
    parser.add_argument("--session-pid", type=int, required=True)
    parser.add_argument(
        "--x64dbg-path",
        default=r"D:\Scoop\apps\x64dbg\current\release\x64\x64dbg.exe",
    )
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--vector-address", type=parse_int)
    source.add_argument(
        "--vector-register",
        help="x64dbg register/expression that evaluates to the vector object",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("chat_history_export.jsonl"),
    )
    parser.add_argument("--max-messages", type=int, default=100_000)
    parser.add_argument(
        "--metadata-only",
        action="store_true",
        help="Blank message_content, compress_content and source in the JSONL.",
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
                "debuggee is running; pause at a result-vector breakpoint first"
            )

        vector_address = resolve_vector_address(
            client,
            args.vector_address,
            args.vector_register,
        )
        begin, count = validate_vector(client, vector_address, args.max_messages)

        args.output.parent.mkdir(parents=True, exist_ok=True)
        with args.output.open("w", encoding="utf-8", newline="\n") as output:
            for index in range(count):
                address = begin + index * CHAT_MESSAGE_SIZE
                record = decode_message(client, address, index)
                if args.metadata_only:
                    record["message_content"] = ""
                    record["compress_content"] = ""
                    record["source"] = ""
                output.write(json.dumps(record, ensure_ascii=False) + "\n")

        print(
            json.dumps(
                {
                    "vector_address": f"0x{vector_address:X}",
                    "begin": f"0x{begin:X}" if begin else None,
                    "count": count,
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
