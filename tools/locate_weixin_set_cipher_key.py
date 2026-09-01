#!/usr/bin/env python3
"""Locate Weixin.dll's WCDB SetCipherKey wrapper without external packages."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
from dataclasses import dataclass
from pathlib import Path


PATTERNS = {
    "entry": "55 41 57 41 56 56 57 53 48 83 EC 58 48 8D 6C 24 ?? "
    "48 C7 45 ?? ?? ?? ?? ?? 44 89 CF 44 89 C3 49 89 D6",
    "configure": "49 83 C7 10 4C 89 F9 4C 89 F2 41 89 D8 41 89 F9 "
    "E8 ?? ?? ?? ?? 90",
    "install": "48 8D 15 ?? ?? ?? ?? 4C 8D 45 ?? 48 89 F1 41 B9 "
    "00 00 00 80 E8 ?? ?? ?? ??",
    "remove": "48 8D 15 ?? ?? ?? ?? 48 89 F1 48 83 C4 58",
}
CONFIG_NAME = b"com.Tencent.WCDB.Config.Cipher"


@dataclass(frozen=True)
class Section:
    name: str
    virtual_address: int
    virtual_size: int
    raw_offset: int
    raw_size: int


class PeImage:
    def __init__(self, data: bytes) -> None:
        self.data = data
        if data[:2] != b"MZ":
            raise ValueError("not a PE image: DOS signature is missing")
        pe_offset = struct.unpack_from("<I", data, 0x3C)[0]
        if data[pe_offset : pe_offset + 4] != b"PE\0\0":
            raise ValueError("not a PE image: NT signature is missing")

        section_count = struct.unpack_from("<H", data, pe_offset + 6)[0]
        optional_size = struct.unpack_from("<H", data, pe_offset + 20)[0]
        optional_offset = pe_offset + 24
        if struct.unpack_from("<H", data, optional_offset)[0] != 0x20B:
            raise ValueError("expected a 64-bit PE32+ image")
        self.image_base = struct.unpack_from("<Q", data, optional_offset + 24)[0]

        section_offset = optional_offset + optional_size
        sections: list[Section] = []
        for index in range(section_count):
            offset = section_offset + index * 40
            name = data[offset : offset + 8].split(b"\0", 1)[0].decode("ascii", "replace")
            virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from(
                "<IIII", data, offset + 8
            )
            sections.append(
                Section(name, virtual_address, virtual_size, raw_offset, raw_size)
            )
        self.sections = sections

    def section(self, name: str) -> Section:
        return next(section for section in self.sections if section.name == name)

    def offset_to_rva(self, offset: int) -> int:
        for section in self.sections:
            if section.raw_offset <= offset < section.raw_offset + section.raw_size:
                return section.virtual_address + offset - section.raw_offset
        raise ValueError(f"file offset {offset:#x} is outside mapped sections")

    def runtime_function(self, rva: int) -> tuple[int, int]:
        pdata = self.section(".pdata")
        for offset in range(pdata.raw_offset, pdata.raw_offset + pdata.raw_size - 11, 12):
            begin, end, _ = struct.unpack_from("<III", self.data, offset)
            if begin <= rva < end:
                return begin, end
        raise ValueError(f"RVA {rva:#x} is not covered by .pdata")

    def rip_references(self, target_rva: int) -> list[int]:
        text = self.section(".text")
        code = self.data[text.raw_offset : text.raw_offset + text.raw_size]
        instruction = re.compile(
            rb"[\x40-\x4f][\x8b\x89\x8d][\x05\x0d\x15\x1d\x25\x2d\x35\x3d].{4}",
            re.DOTALL,
        )
        references: list[int] = []
        for match in instruction.finditer(code):
            instruction_rva = text.virtual_address + match.start()
            displacement = struct.unpack_from("<i", code, match.start() + 3)[0]
            if instruction_rva + 7 + displacement == target_rva:
                references.append(instruction_rva)
        return references


def compile_pattern(pattern: str) -> re.Pattern[bytes]:
    return re.compile(
        b"".join(
            b"." if byte == "??" else re.escape(bytes.fromhex(byte))
            for byte in pattern.split()
        ),
        re.DOTALL,
    )


def unique_pattern_rva(image: PeImage, name: str) -> int:
    matches = list(compile_pattern(PATTERNS[name]).finditer(image.data))
    if len(matches) != 1:
        raise ValueError(f"{name} pattern matched {len(matches)} locations; expected one")
    return image.offset_to_rva(matches[0].start())


def locate(path: Path) -> dict[str, object]:
    data = path.read_bytes()
    image = PeImage(data)
    pattern_rvas = {name: unique_pattern_rva(image, name) for name in PATTERNS}
    function_begin, function_end = image.runtime_function(pattern_rvas["configure"])
    if pattern_rvas["entry"] != function_begin:
        raise ValueError("entry signature does not start at the .pdata function boundary")
    if any(
        not function_begin <= rva < function_end
        for name, rva in pattern_rvas.items()
        if name != "entry"
    ):
        raise ValueError("semantic check patterns do not belong to one runtime function")

    install_offset = next(compile_pattern(PATTERNS["install"]).finditer(data)).start()
    remove_offset = next(compile_pattern(PATTERNS["remove"]).finditer(data)).start()
    install_target = pattern_rvas["install"] + 7 + struct.unpack_from(
        "<i", data, install_offset + 3
    )[0]
    remove_target = pattern_rvas["remove"] + 7 + struct.unpack_from(
        "<i", data, remove_offset + 3
    )[0]
    if install_target != remove_target:
        raise ValueError("install and remove paths reference different configuration keys")

    config_offset = data.find(CONFIG_NAME)
    if config_offset < 0 or data.find(CONFIG_NAME, config_offset + 1) >= 0:
        raise ValueError("WCDB cipher configuration name is missing or non-unique")
    config_rva = image.offset_to_rva(config_offset)
    config_references = image.rip_references(config_rva)
    if len(config_references) != 1:
        raise ValueError(
            f"configuration name has {len(config_references)} code references; expected one"
        )
    registration_begin, registration_end = image.runtime_function(config_references[0])

    return {
        "path": str(path.resolve()),
        "sha256": hashlib.sha256(data).hexdigest().upper(),
        "image_base": f"0x{image.image_base:X}",
        "set_cipher_key_rva": f"0x{function_begin:X}",
        "set_cipher_key_end_rva": f"0x{function_end:X}",
        "pattern_rvas": {name: f"0x{rva:X}" for name, rva in pattern_rvas.items()},
        "cipher_config_name_rva": f"0x{config_rva:X}",
        "cipher_config_key_rva": f"0x{install_target:X}",
        "registration_function_rva": f"0x{registration_begin:X}",
        "registration_function_end_rva": f"0x{registration_end:X}",
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("weixin_dll", type=Path)
    args = parser.parse_args()
    try:
        result = locate(args.weixin_dll)
    except (OSError, StopIteration, struct.error, ValueError) as error:
        parser.error(str(error))
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
