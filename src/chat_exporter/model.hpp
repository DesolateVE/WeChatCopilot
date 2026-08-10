#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace wechat::chat_exporter {

struct ContactMatch {
  int64_t id = 0;
  std::string username;
  std::string alias;
  std::string remark;
  std::string nickname;
  int priority = 99;
};

struct ExportResult {
  std::string name;
  std::filesystem::path file;
  int64_t rows = 0;
};

} // namespace wechat::chat_exporter
