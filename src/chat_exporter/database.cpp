#define NOMINMAX
// clang-format off: wincrypt.h depends on Windows base types.
#include <windows.h>
#include <wincrypt.h>
// clang-format on

#include "database.hpp"

#include <fstream>
#include <stdexcept>

namespace wechat::chat_exporter {
namespace {

constexpr int kCipherPageSize = 4096;

std::string pathUtf8(const std::filesystem::path &path) {
  const auto value = path.u8string();
  return std::string(value.begin(), value.end());
}

template <typename TextView> std::string wcdbText(const TextView &value) {
  return std::string(value.data(), value.length());
}

std::string base64Encode(const unsigned char *data, const size_t size) {
  if (size == 0) {
    return {};
  }
  if (size > MAXDWORD) {
    throw std::runtime_error("BLOB is too large for Base64 encoding");
  }
  DWORD characters = 0;
  constexpr DWORD flags = CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF;
  if (CryptBinaryToStringA(data, static_cast<DWORD>(size), flags, nullptr,
                           &characters) == FALSE) {
    throw std::runtime_error("cannot determine Base64 output size");
  }
  std::string encoded(characters, '\0');
  if (CryptBinaryToStringA(data, static_cast<DWORD>(size), flags,
                           encoded.data(), &characters) == FALSE) {
    throw std::runtime_error("cannot encode BLOB as Base64");
  }
  encoded.resize(characters);
  if (!encoded.empty() && encoded.back() == '\0') {
    encoded.pop_back();
  }
  return encoded;
}

} // namespace

ReadOnlyDatabase::ReadOnlyDatabase(const std::filesystem::path &path,
                                   const std::vector<unsigned char> &key,
                                   const bool configureMessageCompression)
    : path_(path), database_(pathUtf8(std::filesystem::absolute(path)), true) {
  if (!std::filesystem::is_regular_file(path_)) {
    throw std::runtime_error("database does not exist: " + pathUtf8(path_));
  }
  database_.setCipherKey(WCDB::UnsafeData::immutable(key.data(), key.size()),
                         kCipherPageSize,
                         WCDB::Database::CipherVersion::Version4);
  if (configureMessageCompression) {
    database_.setCompression([](WCDB::Database::CompressionInfo &info) {
      if (info.getTableName().hasPrefix("Msg_")) {
        info.addZSTDNormalCompressField(
            WCDB::Field("message_content", nullptr));
        info.addZSTDNormalCompressField(WCDB::Field("source", nullptr));
      }
    });
  }
  if (!database_.canOpen()) {
    throw std::runtime_error("WCDB cannot open " + pathUtf8(path_) + ": " +
                             wcdbText(database_.getError().getMessage()));
  }
}

bool ReadOnlyDatabase::tableExists(const std::string &tableName) {
  const auto statement =
      WCDB::StatementSelect()
          .select(1)
          .from(WCDB::TableOrSubquery::master())
          .where(WCDB::Column("type") == "table" &&
                 WCDB::Column("name") == WCDB::BindParameter())
          .limit(1);
  bool found = false;
  forEach(statement, {tableName}, [&](WCDB::Handle &) { found = true; });
  return found;
}

void ReadOnlyDatabase::forEach(
    const WCDB::Statement &statement, const std::vector<Binding> &bindings,
    const std::function<void(WCDB::Handle &)> &callback) {
  WCDB::Handle handle = database_.getHandle();
  try {
    if (!handle.prepare(statement)) {
      throw std::runtime_error(
          "WCDB prepare failed: " + wcdbText(handle.getError().getMessage()) +
          " | " + wcdbText(statement.getDescription()));
    }
    int index = 1;
    for (const auto &binding : bindings) {
      if (std::holds_alternative<int64_t>(binding)) {
        handle.bindInteger(std::get<int64_t>(binding), index);
      } else {
        handle.bindText(std::get<std::string>(binding), index);
      }
      ++index;
    }
    bool succeeded = handle.step();
    while (succeeded && !handle.done()) {
      callback(handle);
      succeeded = handle.step();
    }
    if (!succeeded) {
      throw std::runtime_error(
          "WCDB step failed: " + wcdbText(handle.getError().getMessage()) +
          " | " + wcdbText(statement.getDescription()));
    }
    handle.finalize();
    handle.invalidate();
  } catch (...) {
    if (handle.isPrepared()) {
      handle.finalize();
    }
    handle.invalidate();
    throw;
  }
}

WCDB::Column column(const char *name, const char *tableName) {
  WCDB::Column result(name);
  if (tableName != nullptr) {
    result.table(tableName);
  }
  return result;
}

WCDB::TableOrSubquery table(const char *name, const char *alias) {
  WCDB::TableOrSubquery result(name);
  if (alias != nullptr) {
    result.as(alias);
  }
  return result;
}

nlohmann::json rowToJson(WCDB::Handle &handle) {
  nlohmann::json row = nlohmann::json::object();
  const int columns = handle.getNumberOfColumns();
  for (int index = 0; index < columns; ++index) {
    const std::string name = wcdbText(handle.getColumnName(index));
    switch (handle.getType(index)) {
    case WCDB::ColumnType::Null:
      row[name] = nullptr;
      break;
    case WCDB::ColumnType::Integer:
      row[name] = handle.getInteger(index);
      break;
    case WCDB::ColumnType::Float:
      row[name] = handle.getDouble(index);
      break;
    case WCDB::ColumnType::Text:
      row[name] = wcdbText(handle.getText(index));
      break;
    case WCDB::ColumnType::BLOB: {
      const auto blob = handle.getBLOB(index);
      row[name] = {
          {"encoding", "base64"},
          {"bytes", blob.size()},
          {"data", base64Encode(blob.buffer(), blob.size())},
      };
      break;
    }
    default:
      throw std::runtime_error("unknown WCDB column type");
    }
  }
  return row;
}

std::string textAt(WCDB::Handle &handle, const int index) {
  return handle.getType(index) == WCDB::ColumnType::Null
             ? std::string()
             : wcdbText(handle.getText(index));
}

} // namespace wechat::chat_exporter
