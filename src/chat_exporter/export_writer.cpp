#include "export_writer.hpp"

#include <cstdint>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace wechat::chat_exporter {
namespace {

std::string pathUtf8(const std::filesystem::path &path) {
  const auto value = path.u8string();
  return std::string(value.begin(), value.end());
}

std::string isoUtc(const int64_t timestampValue) {
  if (timestampValue <= 0) {
    return {};
  }
  const std::time_t value = static_cast<std::time_t>(timestampValue);
  std::tm calendar{};
  if (gmtime_s(&calendar, &value) != 0) {
    return {};
  }
  std::ostringstream stream;
  stream << std::put_time(&calendar, "%Y-%m-%dT%H:%M:%SZ");
  return stream.str();
}

} // namespace

void ensureFreshOutputDirectory(const std::filesystem::path &directory) {
  if (std::filesystem::exists(directory)) {
    if (!std::filesystem::is_directory(directory) ||
        !std::filesystem::is_empty(directory)) {
      throw std::runtime_error("output directory must be empty");
    }
  } else if (!std::filesystem::create_directories(directory)) {
    throw std::runtime_error("cannot create output directory");
  }
}

ExportResult exportQuery(ReadOnlyDatabase &database, const std::string &name,
                         const WCDB::Statement &statement,
                         const std::vector<Binding> &bindings,
                         const std::filesystem::path &outputDirectory,
                         const std::function<void(nlohmann::json &)> &augment) {
  const auto path = outputDirectory / std::filesystem::u8path(name + ".jsonl");
  if (std::filesystem::exists(path)) {
    throw std::runtime_error("refusing to overwrite: " + pathUtf8(path));
  }
  std::ofstream output(path, std::ios::binary);
  if (!output) {
    throw std::runtime_error("cannot create output: " + pathUtf8(path));
  }
  int64_t rows = 0;
  database.forEach(statement, bindings, [&](WCDB::Handle &handle) {
    auto row = rowToJson(handle);
    if (augment) {
      augment(row);
    }
    output << row.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)
           << '\n';
    ++rows;
  });
  return {name, path, rows};
}

ExportResult createEmptyExport(const std::string &name,
                               const std::filesystem::path &outputDirectory) {
  const auto path = outputDirectory / std::filesystem::u8path(name + ".jsonl");
  std::ofstream output(path, std::ios::binary);
  if (!output) {
    throw std::runtime_error("cannot create output: " + pathUtf8(path));
  }
  return {name, path, 0};
}

WCDB::StatementSelect selectAllWhere(const char *tableName,
                                     const char *columnName,
                                     const int parameter) {
  return WCDB::StatementSelect()
      .select(WCDB::Column::all())
      .from(tableName)
      .where(column(columnName) == WCDB::BindParameter(parameter));
}

ExportResult exportMessages(
    ReadOnlyDatabase &database, const std::string &name,
    const std::string &username, const bool isGroup,
    const std::string &messageTable,
    const std::filesystem::path &outputDirectory,
    const std::function<void(nlohmann::json &)> &augment) {
  if (!database.tableExists(messageTable)) {
    return createEmptyExport(name, outputDirectory);
  }
  auto messageSource = table(messageTable.c_str(), "m");
  auto senderSource = table("Name2Id", "sender");
  auto join =
      WCDB::Join()
          .table(messageSource)
          .leftJoin(senderSource)
          .on(column("rowid", "sender") == column("real_sender_id", "m"));
  const auto statement =
      WCDB::StatementSelect()
          .select({WCDB::Column::all().table("m"),
                   column("user_name", "sender").as("real_sender_username")})
          .from(join)
          .orders({column("sort_seq", "m").asAscOrder(),
                   column("local_id", "m").asAscOrder()});
  return exportQuery(
      database, name, statement, {}, outputDirectory, [&](nlohmann::json &row) {
        row["conversation_username"] = username;
        row["conversation_kind"] = isGroup ? "group" : "user";
        if (row.contains("local_type") &&
            row["local_type"].is_number_integer()) {
          const auto localType = row["local_type"].get<uint64_t>();
          row["local_type_base"] = localType & 0xFFFFFFFFULL;
          row["local_type_flags"] = localType >> 32;
        }
        if (row.contains("create_time") &&
            row["create_time"].is_number_integer()) {
          const auto value = isoUtc(row["create_time"].get<int64_t>());
          row["create_time_utc"] =
              value.empty() ? nlohmann::json(nullptr) : nlohmann::json(value);
        }
        if (augment) {
          augment(row);
        }
      });
}

void writeManifest(const Options &options, const ContactMatch &contact,
                   const bool isGroup, const std::string &messageTable,
                   const std::vector<ExportResult> &exports) {
  int64_t totalRows = 0;
  nlohmann::json files = nlohmann::json::array();
  for (const auto &item : exports) {
    totalRows += item.rows;
    files.push_back({
        {"name", item.name},
        {"file", pathUtf8(item.file.filename())},
        {"rows", item.rows},
    });
  }
  const nlohmann::json manifest{
      {"format_version", 2},
      {"read_only", true},
      {"query", options.query},
      {"resolved_contact",
       {
           {"username", contact.username},
           {"alias", contact.alias},
           {"remark", contact.remark},
           {"nick_name", contact.nickname},
       }},
      {"conversation_kind", isGroup ? "group" : "user"},
      {"message_table", messageTable},
      {"total_exported_rows", totalRows},
      {"files", files},
  };
  std::ofstream output(options.outputDirectory / "manifest.json",
                       std::ios::binary);
  if (!output) {
    throw std::runtime_error("cannot create manifest.json");
  }
  output << manifest.dump(2, ' ', false,
                          nlohmann::json::error_handler_t::replace)
         << '\n';
}

} // namespace wechat::chat_exporter
