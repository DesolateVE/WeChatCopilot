#pragma once

#include "database.hpp"
#include "model.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace wechat::chat_exporter {

void ensureFreshOutputDirectory(const std::filesystem::path &directory);

ExportResult
exportQuery(ReadOnlyDatabase &database, const std::string &name,
            const WCDB::Statement &statement,
            const std::vector<Binding> &bindings,
            const std::filesystem::path &outputDirectory,
            const std::function<void(nlohmann::json &)> &augment = {});

ExportResult createEmptyExport(const std::string &name,
                               const std::filesystem::path &outputDirectory);

WCDB::StatementSelect selectAllWhere(const char *tableName,
                                     const char *columnName, int parameter = 1);

ExportResult exportMessages(
    ReadOnlyDatabase &database, const std::string &name,
    const std::string &username, bool isGroup, const std::string &messageTable,
    const std::filesystem::path &outputDirectory,
    const std::function<void(nlohmann::json &)> &augment = {});

void writeManifest(const std::filesystem::path &outputDirectory,
                   const ContactMatch &contact,
                   bool isGroup, const std::string &messageTable,
                   const std::vector<ExportResult> &exports);

} // namespace wechat::chat_exporter
