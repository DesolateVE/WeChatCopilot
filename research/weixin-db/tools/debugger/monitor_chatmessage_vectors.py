#!/usr/bin/env python3
"""Capture Weixin ChatMessage result vectors and automatically resume x64dbg.

The monitor is intentionally narrow: it only handles breakpoints whose addresses
are explicitly supplied with --result-address. An unrelated breakpoint is left
paused and causes the monitor to exit instead of resuming it.
"""

from __future__ import annotations

import argparse
import json
import time
from pathlib import Path
from typing import Any

from x64dbg_automate import X64DbgClient
from x64dbg_automate.events import EventType

from extract_chatmessage_vector import (
    CHAT_MESSAGE_SIZE,
    decode_message,
    parse_int,
    validate_vector,
)


DEFAULT_PAGED_RESULT = 0x7FF909797C5E


def record_key(record: dict[str, Any]) -> tuple[Any, ...]:
    return (
        record.get("session_username_0x38"),
        record.get("local_id"),
        record.get("server_id"),
        record.get("sort_seq"),
    )


def load_existing_keys(path: Path) -> set[tuple[Any, ...]]:
    keys: set[tuple[Any, ...]] = set()
    if not path.exists():
        return keys
    with path.open("r", encoding="utf-8") as existing:
        for line_number, line in enumerate(existing, 1):
            if not line.strip():
                continue
            try:
                keys.add(record_key(json.loads(line)))
            except json.JSONDecodeError as exc:
                raise ValueError(
                    f"invalid JSONL at {path}:{line_number}: {exc}"
                ) from exc
    return keys


def wait_until_paused(client: X64DbgClient, timeout_seconds: float = 5.0) -> bool:
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        if not client.is_running():
            return True
        time.sleep(0.05)
    return not client.is_running()


def capture_current_vector(
    client: X64DbgClient,
    output_path: Path,
    seen: set[tuple[Any, ...]],
    batch_index: int,
    breakpoint_address: int,
    max_messages: int,
    metadata_only: bool,
) -> tuple[int, int]:
    vector_address = client.get_reg("rdx")
    begin, count = validate_vector(client, vector_address, max_messages)
    added = 0

    with output_path.open("a", encoding="utf-8", newline="\n") as output:
        for index in range(count):
            address = begin + index * CHAT_MESSAGE_SIZE
            record = decode_message(client, address, index)
            key = record_key(record)
            if key in seen:
                continue
            seen.add(key)
            record["capture_batch"] = batch_index
            record["capture_breakpoint"] = f"0x{breakpoint_address:X}"
            if metadata_only:
                record["message_content"] = ""
                record["compress_content"] = ""
                record["source"] = ""
            output.write(json.dumps(record, ensure_ascii=False) + "\n")
            added += 1
    return count, added


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Monitor known x64dbg ChatMessage result breakpoints."
    )
    parser.add_argument("--session-pid", type=int, required=True)
    parser.add_argument(
        "--x64dbg-path",
        default=r"D:\Scoop\apps\x64dbg\current\release\x64\x64dbg.exe",
    )
    parser.add_argument(
        "--result-address",
        type=parse_int,
        action="append",
        default=[],
        help="Known result-vector breakpoint; may be supplied multiple times.",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("artifacts/exports/date_query_capture.jsonl"),
    )
    parser.add_argument("--idle-timeout", type=float, default=600.0)
    parser.add_argument("--max-batches", type=int, default=100)
    parser.add_argument("--max-messages", type=int, default=100_000)
    parser.add_argument("--metadata-only", action="store_true")
    return parser


def main() -> int:
    args = build_parser().parse_args()
    result_addresses = set(args.result_address or [DEFAULT_PAGED_RESULT])
    args.output.parent.mkdir(parents=True, exist_ok=True)
    seen = load_existing_keys(args.output)

    client = X64DbgClient(args.x64dbg_path)
    client.attach_session(args.session_pid)
    try:
        if not client.is_debugging():
            raise RuntimeError("the selected x64dbg session has no debuggee")

        client.clear_debug_events()
        last_capture = time.monotonic()
        batch_index = 0
        print(
            json.dumps(
                {
                    "status": "monitoring",
                    "result_addresses": [
                        f"0x{address:X}" for address in sorted(result_addresses)
                    ],
                    "existing_records": len(seen),
                    "output": str(args.output.resolve()),
                },
                ensure_ascii=False,
            ),
            flush=True,
        )

        while batch_index < args.max_batches:
            if time.monotonic() - last_capture >= args.idle_timeout:
                print(
                    json.dumps(
                        {
                            "status": "idle_timeout",
                            "captured_batches": batch_index,
                            "total_records": len(seen),
                        }
                    ),
                    flush=True,
                )
                return 0

            if not client.is_running():
                rip = client.get_reg("rip")
                if rip not in result_addresses:
                    print(
                        json.dumps(
                            {
                                "status": "unrelated_pause",
                                "rip": f"0x{rip:X}",
                                "action": "left_paused",
                            }
                        ),
                        flush=True,
                    )
                    return 2

                count, added = capture_current_vector(
                    client,
                    args.output,
                    seen,
                    batch_index,
                    rip,
                    args.max_messages,
                    args.metadata_only,
                )
                print(
                    json.dumps(
                        {
                            "status": "captured",
                            "batch": batch_index,
                            "breakpoint": f"0x{rip:X}",
                            "vector_count": count,
                            "new_records": added,
                            "total_records": len(seen),
                        }
                    ),
                    flush=True,
                )
                batch_index += 1
                last_capture = time.monotonic()
                client.go()
                continue

            event = client.wait_for_debug_event(
                EventType.EVENT_BREAKPOINT,
                timeout=1,
            )
            if event is None:
                continue
            address = event.event_data.addr
            if address not in result_addresses:
                if wait_until_paused(client):
                    print(
                        json.dumps(
                            {
                                "status": "unrelated_breakpoint",
                                "address": f"0x{address:X}",
                                "action": "left_paused",
                            }
                        ),
                        flush=True,
                    )
                    return 2

        print(
            json.dumps(
                {
                    "status": "max_batches",
                    "captured_batches": batch_index,
                    "total_records": len(seen),
                }
            ),
            flush=True,
        )
        return 0
    finally:
        client.detach_session()


if __name__ == "__main__":
    raise SystemExit(main())
