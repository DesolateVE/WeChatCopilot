#include "database_access.hpp"

#include <WCDB/WCDBCpp.h>
#define ZSTD_STATIC_LINKING_ONLY
#include <zstd.h>

#include <algorithm>
#include <iostream>
#include <optional>
#include <utility>

namespace wechat {
namespace {

using Json = nlohmann::json;

constexpr size_t kMaximumDecompressedBytes = 512ULL * 1024ULL * 1024ULL;

std::optional<std::filesystem::path> resolveDatabasePath(
    const std::filesystem::path& rootDirectory, const std::string& relativePath)
{
    if (relativePath.empty()) {
        return std::nullopt;
    }

    std::error_code error;
    const auto root = std::filesystem::weakly_canonical(rootDirectory, error);
    const auto candidate = std::filesystem::weakly_canonical(root / std::filesystem::path(relativePath), error);
    if (error || candidate.extension() != ".db" || !std::filesystem::is_regular_file(candidate, error)) {
        return std::nullopt;
    }

    const auto relative = std::filesystem::relative(candidate, root, error);
    if (error || relative.empty() || *relative.begin() == "..") {
        return std::nullopt;
    }
    return candidate;
}

std::vector<std::string> listTablesFromHandle(WCDB::Handle& handle)
{
    auto statement = WCDB::StatementSelect()
                         .select(WCDB::Column("name"))
                         .from(WCDB::TableOrSubquery::master())
                         .where(WCDB::Column("type") == "table");
    auto rows = handle.getAllRowsFromStatement(statement);
    std::vector<std::string> tables;
    if (!rows.succeed()) {
        return tables;
    }

    for (const auto& row : rows.value()) {
        if (row.empty()) {
            continue;
        }
        std::string tableName = std::string(row[0].textValue());
        if (tableName != "sqlite_sequence") {
            tables.push_back(std::move(tableName));
        }
    }
    std::sort(tables.begin(), tables.end());
    return tables;
}

Json tableSchemaFromHandle(WCDB::Handle& handle, const std::string& tableName)
{
    Json columns = Json::array();
    auto pragma = WCDB::StatementPragma().pragma(WCDB::Pragma::tableInfo()).with(tableName);
    auto rows = handle.getAllRowsFromStatement(pragma);
    if (!rows.succeed()) {
        return Json { { "name", tableName }, { "columns", columns },
            { "error", std::string(handle.getError().getMessage()) } };
    }

    for (const auto& row : rows.value()) {
        if (row.size() < 6) {
            continue;
        }
        columns.push_back({ { "cid", row[0].intValue() },
            { "name", std::string(row[1].textValue()) },
            { "type", std::string(row[2].textValue()) },
            { "notNull", row[3].intValue() != 0 },
            { "defaultValue", row[4].isNull() ? Json(nullptr) : Json(std::string(row[4].textValue())) },
            { "primaryKey", row[5].intValue() } });
    }

    auto sqlStatement = WCDB::StatementSelect()
                            .select(WCDB::Column("sql"))
                            .from(WCDB::TableOrSubquery::master())
                            .where(WCDB::Column("type") == "table" && WCDB::Column("name") == tableName);
    auto sql = handle.getValueFromStatement(sqlStatement);
    return Json { { "name", tableName },
        { "sql", sql.succeed() && !sql.value().isNull() ? std::string(sql.value().textValue()) : "" },
        { "columns", std::move(columns) } };
}

Json databaseSchemaFromHandle(WCDB::Handle& handle, const std::string& databaseName)
{
    Json tables = Json::array();
    for (const auto& tableName : listTablesFromHandle(handle)) {
        tables.push_back(tableSchemaFromHandle(handle, tableName));
    }
    return Json { { "database", databaseName }, { "tables", std::move(tables) } };
}

std::optional<std::string> decompressZstd(const WCDB::Data& compressed)
{
    if (compressed.empty()) {
        return std::nullopt;
    }

    const auto expected = ZSTD_getFrameContentSize(compressed.buffer(), compressed.size());
    if (expected == ZSTD_CONTENTSIZE_ERROR) {
        return std::nullopt;
    }

    const size_t outputSize = expected == ZSTD_CONTENTSIZE_UNKNOWN
        ? ZSTD_decompressBound(compressed.buffer(), compressed.size())
        : expected > kMaximumDecompressedBytes ? ZSTD_CONTENTSIZE_ERROR
                                               : static_cast<size_t>(expected);
    if (ZSTD_isError(outputSize) != 0 || outputSize > kMaximumDecompressedBytes) {
        return std::nullopt;
    }

    std::string output(outputSize, '\0');
    const size_t actual = ZSTD_decompress(
        output.data(), output.size(), compressed.buffer(), compressed.size());
    if (ZSTD_isError(actual) != 0) {
        return std::nullopt;
    }
    output.resize(actual);
    return output;
}

std::string displayValue(const WCDB::Value& value)
{
    if (value.isNull()) {
        return { };
    }
    if (value.getType() == WCDB::Value::Type::BLOB) {
        constexpr char hexDigits[] = "0123456789ABCDEF";
        const auto blob = value.blobValue();
        std::string encoded;
        encoded.reserve(blob.empty() ? 0 : blob.size() * 3 - 1);
        for (size_t index = 0; index < blob.size(); ++index) {
            if (index != 0) {
                encoded.push_back(' ');
            }
            const auto byte = blob.buffer()[index];
            encoded.push_back(hexDigits[byte >> 4]);
            encoded.push_back(hexDigits[byte & 0x0f]);
        }
        return encoded;
    }
    const auto text = value.textValue();
    return std::string(text.data(), text.size());
}

template<typename T, typename Callback>
DatabaseResult<T> withDatabase(
    const std::filesystem::path& rootDirectory, const std::vector<unsigned char>& keyBytes,
    const std::string& databaseName, Callback callback)
{
    const auto databasePath = resolveDatabasePath(rootDirectory, databaseName);
    if (!databasePath) {
        return { DatabaseStatus::NotFound, { } };
    }

    WCDB::Database database(databasePath->string(), true);
    database.setCipherKey(keyBytes);
    if (!database.canOpen()) {
        return { DatabaseStatus::CannotOpen, { } };
    }
    auto handle = database.getHandle();
    return { DatabaseStatus::Success, callback(handle) };
}

} // namespace

DatabaseAccess::DatabaseAccess(
    std::filesystem::path rootDirectory, std::vector<unsigned char> keyBytes)
    : rootDirectory_(std::move(rootDirectory))
    , keyBytes_(std::move(keyBytes))
{
}

const std::filesystem::path& DatabaseAccess::rootDirectory() const
{
    return rootDirectory_;
}

std::vector<std::string> DatabaseAccess::listDatabases() const
{
    std::vector<std::string> databases;
    std::error_code error;
    for (std::filesystem::recursive_directory_iterator iterator(
             rootDirectory_, std::filesystem::directory_options::skip_permission_denied, error),
        end;
        iterator != end;
        iterator.increment(error)) {
        if (error) {
            error.clear();
            continue;
        }
        if (iterator->is_regular_file(error) && iterator->path().extension() == ".db") {
            databases.push_back(
                std::filesystem::relative(iterator->path(), rootDirectory_).generic_string());
        }
    }
    std::sort(databases.begin(), databases.end());
    return databases;
}

DatabaseResult<std::vector<std::string>> DatabaseAccess::listTables(
    const std::string& databaseName) const
{
    return withDatabase<std::vector<std::string>>(
        rootDirectory_, keyBytes_, databaseName, [](WCDB::Handle& handle) {
            return listTablesFromHandle(handle);
        });
}

DatabaseResult<Json> DatabaseAccess::databaseSchema(const std::string& databaseName) const
{
    return withDatabase<Json>(rootDirectory_, keyBytes_, databaseName,
        [&databaseName](WCDB::Handle& handle) {
            return databaseSchemaFromHandle(handle, databaseName);
        });
}

DatabaseResult<Json> DatabaseAccess::tableSchema(
    const std::string& databaseName, const std::string& tableName) const
{
    auto result = withDatabase<Json>(rootDirectory_, keyBytes_, databaseName,
        [&tableName](WCDB::Handle& handle) {
            const auto tables = listTablesFromHandle(handle);
            if (std::find(tables.begin(), tables.end(), tableName) == tables.end()) {
                return Json();
            }
            return tableSchemaFromHandle(handle, tableName);
        });
    if (result.status == DatabaseStatus::Success && result.value.is_null()) {
        result.status = DatabaseStatus::TableNotFound;
    }
    return result;
}

DatabaseResult<TablePageResult> DatabaseAccess::queryTablePage(
    const std::string& databaseName, const std::string& tableName, size_t page, size_t pageSize) const
{
    return withDatabase<TablePageResult>(rootDirectory_, keyBytes_, databaseName,
        [&tableName, page, pageSize](WCDB::Handle& handle) {
            TablePageResult result;
            result.page = page;
            result.pageSize = pageSize;
            try {
                auto columnsStatement = WCDB::StatementPragma()
                                            .pragma(WCDB::Pragma::tableInfo())
                                            .with(tableName);
                auto columnInfoRows = handle.getAllRowsFromStatement(columnsStatement);
                if (!columnInfoRows.succeed()) {
                    result.error = std::string(handle.getError().getMessage());
                    std::cerr << "Failed to read columns for " << tableName << ": " << result.error << std::endl;
                    return result;
                }
                for (const auto& columnInfo : columnInfoRows.value()) {
                    if (columnInfo.size() >= 6) {
                        result.columns.emplace_back(columnInfo[1].textValue());
                        result.columnTypes.emplace_back(columnInfo[2].textValue());
                        result.columnPrimaryKeys.push_back(columnInfo[5].intValue());
                    }
                }

                auto countStatement = WCDB::StatementSelect()
                                          .select(WCDB::Column::all().count())
                                          .from(tableName);
                auto countRows = handle.getValueFromStatement(countStatement);
                if (!countRows.succeed()) {
                    result.error = std::string(handle.getError().getMessage());
                    std::cerr << "Failed to count rows for " << tableName << ": " << result.error << std::endl;
                    return result;
                }
                result.totalRows = static_cast<size_t>(countRows.value().intValue());

                auto statement = WCDB::StatementSelect()
                                     .select(WCDB::Column::all())
                                     .from(tableName)
                                     .limit(pageSize)
                                     .offset((page - 1) * pageSize);
                auto rows = handle.getAllRowsFromStatement(statement);
                if (!rows.succeed()) {
                    result.error = std::string(handle.getError().getMessage());
                    std::cerr << "Failed to read rows for " << tableName << ": " << result.error << std::endl;
                    return result;
                }

                for (const auto& row : rows.value()) {
                    std::vector<std::string> values;
                    std::vector<bool> decompressedFlags;
                    std::vector<bool> blobFlags;
                    values.reserve(row.size());
                    decompressedFlags.reserve(row.size());
                    blobFlags.reserve(row.size());
                    for (const auto& value : row) {
                        const bool isBlob = value.getType() == WCDB::Value::Type::BLOB;
                        auto decompressed = isBlob ? decompressZstd(value.blobValue()) : std::nullopt;
                        decompressedFlags.push_back(decompressed.has_value());
                        blobFlags.push_back(isBlob && !decompressed.has_value());
                        values.emplace_back(decompressed ? std::move(*decompressed) : displayValue(value));
                    }
                    result.rows.push_back(std::move(values));
                    result.zstdDecompressed.push_back(std::move(decompressedFlags));
                    result.blobHexEncoded.push_back(std::move(blobFlags));
                }
            } catch (const std::exception& exception) {
                result.error = exception.what();
                std::cerr << "Exception while reading table " << tableName << ": " << result.error << std::endl;
            }
            return result;
        });
}

Json DatabaseAccess::schemaCatalog() const
{
    Json databases = Json::array();
    for (const auto& databaseName : listDatabases()) {
        auto schema = databaseSchema(databaseName);
        if (schema.status == DatabaseStatus::CannotOpen) {
            databases.push_back({ { "database", databaseName }, { "tables", Json::array() },
                { "error", "Database could not be opened" } });
        } else if (schema.status == DatabaseStatus::Success) {
            databases.push_back(std::move(schema.value));
        }
    }
    return Json { { "formatVersion", 1 }, { "root", rootDirectory_.generic_string() },
        { "databases", std::move(databases) } };
}

} // namespace wechat