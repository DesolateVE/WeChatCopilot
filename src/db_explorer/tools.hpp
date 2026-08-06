#pragma once

#define NOMINMAX
#include <windows.h>

#include <stdint.h>
#include <string>
#include <vector>

namespace wx_plugin {
std::vector<uint8_t> HexStringToBytes(const std::string& hexString);
}
