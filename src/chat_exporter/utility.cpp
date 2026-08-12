#define NOMINMAX
// clang-format off: bcrypt.h depends on Windows base types.
#include <windows.h>
#include <bcrypt.h>
// clang-format on

#include "utility.hpp"

#include <array>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace wechat::chat_exporter {
namespace {

std::vector<unsigned char> decodeKeyHex(const std::string &hex) {
  if (hex.size() != 64) {
    throw std::runtime_error(
        "expected a 32-byte WCDB key encoded as 64 hex characters");
  }
  DWORD byteCount = 0;
  if (CryptStringToBinaryA(hex.data(), static_cast<DWORD>(hex.size()),
                           CRYPT_STRING_HEXRAW, nullptr, &byteCount, nullptr,
                           nullptr) == FALSE) {
    throw std::runtime_error("WCDB key is not valid hexadecimal");
  }
  std::vector<unsigned char> bytes(byteCount);
  if (CryptStringToBinaryA(hex.data(), static_cast<DWORD>(hex.size()),
                           CRYPT_STRING_HEXRAW, bytes.data(), &byteCount,
                           nullptr, nullptr) == FALSE) {
    throw std::runtime_error("cannot decode WCDB key");
  }
  bytes.resize(byteCount);
  if (bytes.size() != 32) {
    throw std::runtime_error("expected a 32-byte WCDB key");
  }
  return bytes;
}

} // namespace

KeyWiper::KeyWiper(std::vector<unsigned char> &bytes) : bytes_(bytes) {}

KeyWiper::~KeyWiper() {
  if (!bytes_.empty()) {
    SecureZeroMemory(bytes_.data(), bytes_.size());
  }
}

std::string pathUtf8(const std::filesystem::path &path) {
  const auto value = path.u8string();
  return std::string(value.begin(), value.end());
}

std::string wideToUtf8(const std::wstring_view value) {
  if (value.empty()) {
    return {};
  }
  const int size = WideCharToMultiByte(
      CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
      static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
  if (size <= 0) {
    throw std::runtime_error("cannot convert command line to UTF-8");
  }
  std::string converted(static_cast<size_t>(size), '\0');
  if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                          static_cast<int>(value.size()), converted.data(),
                          size, nullptr, nullptr) != size) {
    throw std::runtime_error("cannot convert command line to UTF-8");
  }
  return converted;
}

std::vector<unsigned char> loadKey(const Options &options) {
  if (!options.keyHex.empty()) {
    return decodeKeyHex(options.keyHex);
  }
  const char *keyHex = std::getenv("WECHAT_DB_KEY_HEX");
  if (keyHex == nullptr || *keyHex == '\0') {
    throw std::runtime_error(
        "set WECHAT_DB_KEY_HEX or pass --key-record <key>");
  }
  return decodeKeyHex(keyHex);
}

std::string md5Hex(const std::string_view value) {
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  DWORD objectSize = 0;
  DWORD resultSize = 0;
  std::vector<unsigned char> object;
  std::array<unsigned char, 16> digest{};
  const auto check = [](const NTSTATUS status, const char *operation) {
    if (status < 0) {
      throw std::runtime_error(std::string("BCrypt failure: ") + operation);
    }
  };
  try {
    check(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_MD5_ALGORITHM, nullptr,
                                      0),
          "open MD5 provider");
    check(BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                            reinterpret_cast<PUCHAR>(&objectSize),
                            sizeof(objectSize), &resultSize, 0),
          "get MD5 object size");
    object.resize(objectSize);
    check(BCryptCreateHash(algorithm, &hash, object.data(), objectSize, nullptr,
                           0, 0),
          "create MD5 hash");
    check(BCryptHashData(
              hash, reinterpret_cast<PUCHAR>(const_cast<char *>(value.data())),
              static_cast<ULONG>(value.size()), 0),
          "hash value");
    check(BCryptFinishHash(hash, digest.data(),
                           static_cast<ULONG>(digest.size()), 0),
          "finish MD5 hash");
  } catch (...) {
    if (hash != nullptr) {
      BCryptDestroyHash(hash);
    }
    if (algorithm != nullptr) {
      BCryptCloseAlgorithmProvider(algorithm, 0);
    }
    throw;
  }
  BCryptDestroyHash(hash);
  BCryptCloseAlgorithmProvider(algorithm, 0);

  std::ostringstream stream;
  stream << std::hex << std::setfill('0');
  for (const unsigned char byte : digest) {
    stream << std::setw(2) << static_cast<unsigned int>(byte);
  }
  return stream.str();
}

std::string timestamp() {
  const std::time_t now = std::time(nullptr);
  std::tm calendar{};
  if (localtime_s(&calendar, &now) != 0) {
    throw std::runtime_error("cannot create output timestamp");
  }
  std::ostringstream stream;
  stream << std::put_time(&calendar, "%Y%m%d_%H%M%S");
  return stream.str();
}

bool endsWith(const std::string &value, const std::string_view suffix) {
  return value.size() >= suffix.size() &&
         value.compare(value.size() - suffix.size(), suffix.size(), suffix) ==
             0;
}

} // namespace wechat::chat_exporter
