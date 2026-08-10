#pragma once

#include <filesystem>
#include <string>

namespace wechat::chat_exporter {

struct Options {
  std::filesystem::path databaseDirectory;
  std::filesystem::path keyRecord;
  std::string query;
  std::string selectedUsername;
  std::filesystem::path outputDirectory;
};

Options parseOptions(int argc, wchar_t *argv[]);

} // namespace wechat::chat_exporter
