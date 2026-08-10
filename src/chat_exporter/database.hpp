#pragma once

#include <WCDB/WCDBCpp.h>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <variant>
#include <vector>

namespace wechat::chat_exporter {

using Binding = std::variant<int64_t, std::string>;

class ReadOnlyDatabase final {
public:
  ReadOnlyDatabase(const std::filesystem::path &path,
                   const std::vector<unsigned char> &key,
                   bool configureMessageCompression = false);

  bool tableExists(const std::string &table);
  void forEach(const WCDB::Statement &statement,
               const std::vector<Binding> &bindings,
               const std::function<void(WCDB::Handle &)> &callback);

private:
  std::filesystem::path path_;
  WCDB::Database database_;
};

WCDB::Column column(const char *name, const char *table = nullptr);
WCDB::TableOrSubquery table(const char *name, const char *alias = nullptr);
nlohmann::json rowToJson(WCDB::Handle &handle);
std::string textAt(WCDB::Handle &handle, int index);

} // namespace wechat::chat_exporter
