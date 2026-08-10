#include "options.hpp"
#include "utility.hpp"

#include <cstdlib>
#include <stdexcept>

namespace wechat::chat_exporter {

Options parseOptions(const int argc, wchar_t *argv[]) {
  if (argc < 2) {
    throw std::runtime_error("usage: chat_exporter <query> [--db-dir <path>] "
                             "[--key-record <key.json>] [--output <directory>] "
                             "[--select-username <username>]");
  }
  Options options;
  options.query = wideToUtf8(argv[1]);
  if (options.query.empty() || options.query.starts_with("--")) {
    throw std::runtime_error(
        "the first argument must be a non-empty query value");
  }
  if (const char *directory = std::getenv("WECHAT_DB_DIR")) {
    options.databaseDirectory = std::filesystem::u8path(directory);
  } else {
    options.databaseDirectory = "local-data/db-storage";
  }
  for (int index = 2; index < argc; index += 2) {
    if (index + 1 >= argc) {
      throw std::runtime_error("each option requires a value");
    }
    const std::wstring name = argv[index];
    const std::filesystem::path value = argv[index + 1];
    if (name == L"--db-dir") {
      options.databaseDirectory = value;
    } else if (name == L"--key-record") {
      options.keyRecord = value;
    } else if (name == L"--output") {
      options.outputDirectory = value;
    } else if (name == L"--select-username") {
      options.selectedUsername = wideToUtf8(argv[index + 1]);
    } else {
      throw std::runtime_error("unknown option: " + wideToUtf8(name));
    }
  }
  if (options.outputDirectory.empty()) {
    options.outputDirectory =
        std::filesystem::path("local-data") / "exports" /
        std::filesystem::u8path("chat_export_" + md5Hex(options.query) + "_" +
                                timestamp());
  }
  return options;
}

} // namespace wechat::chat_exporter
