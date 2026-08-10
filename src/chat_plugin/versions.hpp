#pragma once
#include <cstdint>
#include <string>
#include <map>

struct WechatFunctionOffset
{
    const uintptr_t setCipherKeyOffset;
};

inline std::map<std::string, WechatFunctionOffset> WechatFunctionOffsetsByVersion = {{"4.1.12.26", {0x5DBF40}}};