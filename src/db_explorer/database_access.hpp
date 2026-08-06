#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace wechat {

enum class DatabaseStatus {
    Success,
    NotFound,
    CannotOpen,
    TableNotFound,
};

template<typename T>
struct DatabaseResult {
    DatabaseStatus status = DatabaseStatus::Success;
    T value;
};

struct TablePageResult {
    std::vector<std::string> columns;
    std::vector<std::string> columnTypes;
    std::vector<int> columnPrimaryKeys;
    std::vector<std::vector<std::string>> rows;
    std::vector<std::vector<bool>> zstdDecompressed;
    std::vector<std::vector<bool>> blobHexEncoded;
    size_t totalRows = 0;
    size_t page = 1;
    size_t pageSize = 20;
    std::string error;
};

class DatabaseAccess {
public:
    DatabaseAccess(std::filesystem::path rootDirectory, std::vector<unsigned char> keyBytes);

    const std::filesystem::path& rootDirectory() const;
    std::vector<std::string> listDatabases() const;
    DatabaseResult<std::vector<std::string>> listTables(const std::string& databaseName) const;
    DatabaseResult<nlohmann::json> databaseSchema(const std::string& databaseName) const;
    DatabaseResult<nlohmann::json> tableSchema(
        const std::string& databaseName, const std::string& tableName) const;
    DatabaseResult<TablePageResult> queryTablePage(
        const std::string& databaseName, const std::string& tableName, size_t page, size_t pageSize) const;
    nlohmann::json schemaCatalog() const;

private:
    std::filesystem::path rootDirectory_;
    std::vector<unsigned char> keyBytes_;
};

} // namespace wechat