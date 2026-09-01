#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
// clang-format off: bcrypt.h depends on Windows base types.
#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>
// clang-format on

#include "utility.hpp"

#include <httplib.h>

#include <array>
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

std::vector<unsigned char> loadKeyFromPlugin() {
  httplib::Client client("127.0.0.1", 6500);
  client.set_connection_timeout(2, 0);
  client.set_read_timeout(3, 0);
  const auto response = client.Get("/key/string");
  if (!response) {
    throw std::runtime_error(
        "cannot connect to http://127.0.0.1:6500/key/string; start Weixin "
        "through chat_launcher and wait for the key to be captured");
  }
  if (response->status != 200) {
    throw std::runtime_error(
        "key service returned HTTP " + std::to_string(response->status) +
        "; wait until Weixin opens an encrypted database");
  }
  std::string keyHex = response->body;
  const auto first = keyHex.find_first_not_of(" \t\r\n");
  const auto last = keyHex.find_last_not_of(" \t\r\n");
  if (first == std::string::npos) {
    keyHex.clear();
  } else {
    keyHex = keyHex.substr(first, last - first + 1);
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
