#pragma once

#include <filesystem>

namespace wechat::chat_exporter {

struct Options {
  std::filesystem::path databaseDirectory;
  std::filesystem::path outputRootDirectory;
};

Options parseOptions(int argc, wchar_t *argv[]);

} // namespace wechat::chat_exporter
