#pragma once
#include <array>
#include <cstdint>
#include <string_view>

struct WechatFunctionOffset
{
    uintptr_t setCipherKeyOffset;
};

struct WechatVersionOffset
{
    std::string_view version;
    WechatFunctionOffset functions;
};

inline constexpr std::array WechatFunctionOffsetsByVersion = {
        WechatVersionOffset{"4.1.12.26", {0x5DBF40}},
        WechatVersionOffset{"4.1.13.12", {0x5ECD00}},
};

// Wildcards cover stack-frame displacements and the initialized local value.
// The semantic checks inside the function remain documented in
// docs/REVERSE_ENGINEERING.md and tools/locate_weixin_set_cipher_key.py.
inline constexpr std::array<int16_t, 34> SetCipherKeyEntrySignature = {
        0x55, 0x41, 0x57, 0x41, 0x56, 0x56, 0x57, 0x53, 0x48, 0x83, 0xEC, 0x58,
        0x48, 0x8D, 0x6C, 0x24, -1,   0x48, 0xC7, 0x45, -1,   -1,   -1,   -1,
        -1,   0x44, 0x89, 0xCF, 0x44, 0x89, 0xC3, 0x49, 0x89, 0xD6,
};

inline constexpr const WechatFunctionOffset* FindWechatFunctionOffsets(std::string_view version)
{
    for (const auto& entry : WechatFunctionOffsetsByVersion)
    {
        if (entry.version == version)
        {
            return &entry.functions;
        }
    }
    return nullptr;
}
