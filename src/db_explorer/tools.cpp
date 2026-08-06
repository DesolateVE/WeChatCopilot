#include "tools.hpp"
#include <stdexcept>

namespace wx_plugin {
std::vector<uint8_t> HexStringToBytes(const std::string& hexString)
{
    DWORD byteCount = 0;
    if (CryptStringToBinaryA(
            hexString.data(), static_cast<DWORD>(hexString.size()), CRYPT_STRING_HEXRAW,
            nullptr, &byteCount, nullptr, nullptr)
        == FALSE) {
        throw std::runtime_error("Failed to convert hex string to bytes");
    }
    std::vector<uint8_t> bytes(byteCount);
    if (CryptStringToBinaryA(
            hexString.data(), static_cast<DWORD>(hexString.size()), CRYPT_STRING_HEXRAW,
            bytes.data(), &byteCount, nullptr, nullptr)
        == FALSE) {
        throw std::runtime_error("Failed to convert hex string to bytes");
    }
    return bytes;
}
}