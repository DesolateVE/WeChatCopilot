#pragma once

#include "options.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace wechat::chat_exporter {

class KeyWiper final {
public:
  explicit KeyWiper(std::vector<unsigned char> &bytes);
  KeyWiper(const KeyWiper &) = delete;
  KeyWiper &operator=(const KeyWiper &) = delete;
  ~KeyWiper();

private:
  std::vector<unsigned char> &bytes_;
};

std::string pathUtf8(const std::filesystem::path &path);
std::string wideToUtf8(std::wstring_view value);
std::vector<unsigned char> loadKey(const Options &options);
std::string md5Hex(std::string_view value);
std::string timestamp();
bool endsWith(const std::string &value, std::string_view suffix);

} // namespace wechat::chat_exporter
