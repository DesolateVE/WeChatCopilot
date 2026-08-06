#include <WCDB/WCDBCpp.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr int kCipherPageSize = 4096;

class KeyBytes final {
public:
    explicit KeyBytes(std::vector<unsigned char>&& bytes)
    : m_bytes(std::move(bytes))
    {
    }

    KeyBytes(const KeyBytes&) = delete;
    KeyBytes& operator=(const KeyBytes&) = delete;

    ~KeyBytes()
    {
        std::fill(
        m_bytes.begin(), m_bytes.end(), static_cast<unsigned char>(0));
    }

    unsigned char* data() { return m_bytes.data(); }
    size_t size() const { return m_bytes.size(); }

private:
    std::vector<unsigned char> m_bytes;
};

std::string readFile(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open key record: " + path.string());
    }
    return std::string(
    std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

int hexNibble(char character)
{
    const unsigned char value = static_cast<unsigned char>(character);
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    const unsigned char lower = static_cast<unsigned char>(std::tolower(value));
    if (lower >= 'a' && lower <= 'f') {
        return lower - 'a' + 10;
    }
    return -1;
}

std::vector<unsigned char> decodeKeyRecord(const std::filesystem::path& path)
{
    const std::string document = readFile(path);
    const std::string marker = "\"key_hex\"";
    const size_t keyPosition = document.find(marker);
    if (keyPosition == std::string::npos) {
        throw std::runtime_error("key record has no key_hex field");
    }

    const size_t colonPosition = document.find(':', keyPosition + marker.size());
    const size_t quoteStart = document.find('"', colonPosition);
    const size_t quoteEnd = document.find('"', quoteStart + 1);
    if (colonPosition == std::string::npos || quoteStart == std::string::npos
        || quoteEnd == std::string::npos) {
        throw std::runtime_error("malformed key_hex field");
    }

    const std::string hex = document.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
    if (hex.size() != 64) {
        throw std::runtime_error("expected a 32-byte WCDB key");
    }

    std::vector<unsigned char> key;
    key.reserve(hex.size() / 2);
    for (size_t index = 0; index < hex.size(); index += 2) {
        const int high = hexNibble(hex[index]);
        const int low = hexNibble(hex[index + 1]);
        if (high < 0 || low < 0) {
            throw std::runtime_error("key_hex contains a non-hex character");
        }
        key.push_back(static_cast<unsigned char>((high << 4) | low));
    }
    return key;
}

std::string databaseError(WCDB::Database& database)
{
    return std::string(database.getError().getDescription().data());
}

bool startsWith(const std::string& value, const std::string_view prefix)
{
    return value.size() >= prefix.size()
           && value.compare(0, prefix.size(), prefix) == 0;
}

std::string jsonEscape(const std::string_view value)
{
    constexpr char hex[] = "0123456789ABCDEF";
    std::string escaped;
    escaped.reserve(value.size());
    for (const unsigned char character : value) {
        switch (character) {
        case '"':
            escaped += "\\\"";
            break;
        case '\\':
            escaped += "\\\\";
            break;
        case '\b':
            escaped += "\\b";
            break;
        case '\f':
            escaped += "\\f";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            if (character < 0x20) {
                escaped += "\\u00";
                escaped += hex[character >> 4];
                escaped += hex[character & 0x0F];
            } else {
                escaped += static_cast<char>(character);
            }
        }
    }
    return escaped;
}

std::vector<std::string> findMessageTables(WCDB::Database& database)
{
    const WCDB::StatementSelect statement
    = WCDB::StatementSelect()
      .select(WCDB::Column("name"))
      .from("sqlite_master")
      .where(WCDB::Column("type") == "table"
             && WCDB::Column("name").like("Msg_%"))
      .order(WCDB::OrderingTerm(WCDB::Column("name")).order(WCDB::Order::ASC));

    WCDB::Handle handle = database.getHandle();
    if (!handle.prepare(statement)) {
        const std::string error = handle.getError().getDescription().data();
        handle.invalidate();
        throw std::runtime_error("cannot enumerate message tables: " + error);
    }

    std::vector<std::string> tables;
    while (handle.step()) {
        if (handle.done()) {
            break;
        }
        tables.emplace_back(handle.getText().data());
    }
    const bool complete = handle.done();
    const std::string error = handle.getError().getDescription().data();
    handle.finalize();
    handle.invalidate();
    if (!complete) {
        throw std::runtime_error("message table enumeration failed: " + error);
    }
    return tables;
}

int64_t countRows(WCDB::Database& database, const std::string& table)
{
    const WCDB::StatementSelect statement
    = WCDB::StatementSelect()
      .select(WCDB::Column::all().count())
      .from(table);

    WCDB::Handle handle = database.getHandle();
    if (!handle.prepare(statement) || !handle.step() || handle.done()) {
        const std::string error = handle.getError().getDescription().data();
        handle.invalidate();
        throw std::runtime_error("cannot count " + table + ": " + error);
    }
    const int64_t count = handle.getInteger();
    handle.finalize();
    handle.invalidate();
    return count;
}

struct SchemaObjectCounts {
    size_t tables = 0;
    size_t indexes = 0;
    size_t views = 0;
    size_t triggers = 0;
    size_t other = 0;
};

std::vector<std::string> findAllTables(
WCDB::Database& database, SchemaObjectCounts& counts)
{
    const WCDB::StatementSelect statement
    = WCDB::StatementSelect()
      .select(WCDB::ResultColumns{
      WCDB::Column("type"), WCDB::Column("name") })
      .from("sqlite_master")
      .order(WCDB::OrderingTerm(WCDB::Column("type")).order(WCDB::Order::ASC));

    WCDB::Handle handle = database.getHandle();
    if (!handle.prepare(statement)) {
        const std::string error = handle.getError().getDescription().data();
        handle.invalidate();
        throw std::runtime_error("cannot enumerate schema objects: " + error);
    }

    std::vector<std::string> tables;
    while (handle.step()) {
        if (handle.done()) {
            break;
        }
        const std::string type = handle.getText(0).data();
        if (type == "table") {
            ++counts.tables;
            tables.emplace_back(handle.getText(1).data());
        } else if (type == "index") {
            ++counts.indexes;
        } else if (type == "view") {
            ++counts.views;
        } else if (type == "trigger") {
            ++counts.triggers;
        } else {
            ++counts.other;
        }
    }
    const bool complete = handle.done();
    const std::string error = handle.getError().getDescription().data();
    handle.finalize();
    handle.invalidate();
    if (!complete) {
        throw std::runtime_error("schema enumeration failed: " + error);
    }
    return tables;
}

struct ColumnSummary {
    std::string name;
    std::string declaredType;
    bool notNull = false;
    bool primaryKey = false;
};

std::vector<ColumnSummary> inspectColumns(
WCDB::Database& database, const std::string& table)
{
    const WCDB::StatementPragma statement
    = WCDB::StatementPragma()
      .pragma(WCDB::Pragma::tableInfo())
      .schema(WCDB::Schema::main())
      .with(table);

    WCDB::Handle handle = database.getHandle();
    if (!handle.prepare(statement)) {
        const std::string error = handle.getError().getDescription().data();
        handle.invalidate();
        throw std::runtime_error("cannot inspect columns for " + table + ": " + error);
    }

    std::vector<ColumnSummary> columns;
    while (handle.step()) {
        if (handle.done()) {
            break;
        }
        ColumnSummary column;
        column.name = handle.getText(1).data();
        column.declaredType = handle.getText(2).data();
        column.notNull = handle.getInteger(3) != 0;
        column.primaryKey = handle.getInteger(5) != 0;
        columns.emplace_back(std::move(column));
    }
    const bool complete = handle.done();
    const std::string error = handle.getError().getDescription().data();
    handle.finalize();
    handle.invalidate();
    if (!complete) {
        throw std::runtime_error("column inspection failed for " + table + ": " + error);
    }
    return columns;
}

enum class TableGroup : size_t {
    Application = 0,
    MessageShard,
    WCDBInternal,
    SQLiteInternal,
    Count,
};

TableGroup classifyTable(const std::string& name)
{
    if (startsWith(name, "Msg_")) {
        return TableGroup::MessageShard;
    }
    if (startsWith(name, "WCDB_") || startsWith(name, "wcdb_")) {
        return TableGroup::WCDBInternal;
    }
    if (startsWith(name, "sqlite_")) {
        return TableGroup::SQLiteInternal;
    }
    return TableGroup::Application;
}

struct TableSummary {
    std::string name;
    std::vector<ColumnSummary> columns;
    int64_t rows = 0;
    TableGroup group = TableGroup::Application;
};

struct GroupSummary {
    const char* name = "";
    size_t tables = 0;
    size_t columns = 0;
    size_t primaryKeyColumns = 0;
    int64_t rows = 0;
    size_t minColumns = std::numeric_limits<size_t>::max();
    size_t maxColumns = 0;
};

void printColumnSample(const TableSummary& table, const size_t maximum)
{
    std::cout << "    {\"table\": \"" << jsonEscape(table.name)
              << "\", \"columns\": [";
    const size_t shown = std::min(maximum, table.columns.size());
    for (size_t index = 0; index < shown; ++index) {
        if (index != 0) {
            std::cout << ", ";
        }
        const ColumnSummary& column = table.columns[index];
        std::cout << "\"" << jsonEscape(column.name);
        if (!column.declaredType.empty()) {
            std::cout << " " << jsonEscape(column.declaredType);
        }
        if (column.primaryKey) {
            std::cout << " PK";
        }
        std::cout << "\"";
    }
    std::cout << "], \"omitted_columns\": " << (table.columns.size() - shown)
              << "}";
}

void printSchemaSummary(WCDB::Database& database)
{
    SchemaObjectCounts objects;
    const std::vector<std::string> tableNames = findAllTables(database, objects);

    std::vector<TableSummary> tables;
    tables.reserve(tableNames.size());
    for (const std::string& tableName : tableNames) {
        TableSummary table;
        table.name = tableName;
        table.group = classifyTable(tableName);
        table.columns = inspectColumns(database, tableName);
        table.rows = countRows(database, tableName);
        tables.emplace_back(std::move(table));
    }

    std::array<GroupSummary, static_cast<size_t>(TableGroup::Count)> groups = {
        GroupSummary{ "application" },
        GroupSummary{ "message_shards" },
        GroupSummary{ "wcdb_internal" },
        GroupSummary{ "sqlite_internal" },
    };
    size_t totalColumns = 0;
    size_t totalPrimaryKeyColumns = 0;
    int64_t totalRows = 0;
    for (const TableSummary& table : tables) {
        GroupSummary& group = groups[static_cast<size_t>(table.group)];
        ++group.tables;
        group.columns += table.columns.size();
        group.rows += table.rows;
        group.minColumns = std::min(group.minColumns, table.columns.size());
        group.maxColumns = std::max(group.maxColumns, table.columns.size());
        totalColumns += table.columns.size();
        totalRows += table.rows;
        for (const ColumnSummary& column : table.columns) {
            if (column.primaryKey) {
                ++group.primaryKeyColumns;
                ++totalPrimaryKeyColumns;
            }
        }
    }

    std::vector<const TableSummary*> largestNonMessageTables;
    const TableSummary* name2Id = nullptr;
    const TableSummary* messageSample = nullptr;
    for (const TableSummary& table : tables) {
        if (table.name == "Name2Id") {
            name2Id = &table;
        }
        if (table.group == TableGroup::MessageShard) {
            if (messageSample == nullptr) {
                messageSample = &table;
            }
        } else {
            largestNonMessageTables.push_back(&table);
        }
    }
    std::sort(largestNonMessageTables.begin(),
              largestNonMessageTables.end(),
              [](const TableSummary* left, const TableSummary* right) {
                  if (left->rows != right->rows) {
                      return left->rows > right->rows;
                  }
                  return left->name < right->name;
              });
    if (largestNonMessageTables.size() > 6) {
        largestNonMessageTables.resize(6);
    }

    std::cout << "{\n"
              << "  \"opened\": true,\n"
              << "  \"read_only\": true,\n"
              << "  \"schema_objects\": {\"tables\": " << objects.tables
              << ", \"indexes\": " << objects.indexes
              << ", \"views\": " << objects.views
              << ", \"triggers\": " << objects.triggers
              << ", \"other\": " << objects.other << "},\n"
              << "  \"totals\": {\"columns\": " << totalColumns
              << ", \"primary_key_columns\": " << totalPrimaryKeyColumns
              << ", \"rows_across_tables\": " << totalRows << "},\n"
              << "  \"table_groups\": [\n";
    for (size_t index = 0; index < groups.size(); ++index) {
        const GroupSummary& group = groups[index];
        const size_t minimum = group.tables == 0 ? 0 : group.minColumns;
        std::cout << "    {\"name\": \"" << group.name
                  << "\", \"tables\": " << group.tables
                  << ", \"columns\": " << group.columns
                  << ", \"primary_key_columns\": " << group.primaryKeyColumns
                  << ", \"rows\": " << group.rows
                  << ", \"columns_per_table\": [" << minimum << ", "
                  << group.maxColumns << "]}";
        std::cout << (index + 1 == groups.size() ? "\n" : ",\n");
    }
    std::cout << "  ],\n"
              << "  \"largest_non_message_tables\": [\n";
    for (size_t index = 0; index < largestNonMessageTables.size(); ++index) {
        const TableSummary& table = *largestNonMessageTables[index];
        std::cout << "    {\"name\": \"" << jsonEscape(table.name)
                  << "\", \"rows\": " << table.rows
                  << ", \"columns\": " << table.columns.size() << "}";
        std::cout << (index + 1 == largestNonMessageTables.size() ? "\n" : ",\n");
    }
    std::cout << "  ],\n"
              << "  \"representative_columns\": [\n";
    bool printed = false;
    if (name2Id != nullptr) {
        printColumnSample(*name2Id, 12);
        printed = true;
    }
    if (messageSample != nullptr) {
        if (printed) {
            std::cout << ",\n";
        }
        printColumnSample(*messageSample, 12);
        printed = true;
    }
    if (printed) {
        std::cout << "\n";
    }
    std::cout << "  ]\n"
              << "}\n";
}

struct DecompressionProbe {
    bool found = false;
    bool returnedText = false;
    size_t utf8Bytes = 0;
    std::string table;
};

DecompressionProbe probeTransparentDecompression(
WCDB::Database& database, const std::vector<std::string>& tables)
{
    for (const std::string& table : tables) {
        const WCDB::StatementSelect statement
        = WCDB::StatementSelect()
          .select(WCDB::Column("message_content"))
          .from(table)
          .where(WCDB::Column("WCDB_CT_message_content") == 4)
          .limit(1);

        WCDB::Handle handle = database.getHandle();
        if (!handle.prepare(statement) || !handle.step()) {
            const std::string error = handle.getError().getDescription().data();
            handle.invalidate();
            throw std::runtime_error(
            "WCDB decompression probe failed for " + table + ": " + error);
        }
        if (!handle.done()) {
            DecompressionProbe probe;
            probe.found = true;
            probe.returnedText = handle.getType() == WCDB::ColumnType::Text;
            probe.table = table;
            if (probe.returnedText) {
                probe.utf8Bytes = handle.getText().length();
            }
            handle.finalize();
            handle.invalidate();
            return probe;
        }
        handle.finalize();
        handle.invalidate();
    }
    return {};
}

int run(const std::filesystem::path& databasePath,
        const std::filesystem::path& keyRecordPath,
        const bool schemaSummary)
{
    KeyBytes key(decodeKeyRecord(keyRecordPath));
    const auto databasePathUtf8Native = databasePath.u8string();
    const std::string databasePathUtf8(
    databasePathUtf8Native.begin(), databasePathUtf8Native.end());

    WCDB::Database database(databasePathUtf8, true);
    database.setCipherKey(
    WCDB::UnsafeData::immutable(key.data(), key.size()),
    kCipherPageSize,
    WCDB::Database::CipherVersion::Version4);

    // Weixin marks normal Zstd-compressed values with WCDB_CT_<column> = 4.
    // WCDB rewrites WINQ SELECT statements and transparently returns text.
    database.setCompression([](WCDB::Database::CompressionInfo& info) {
        if (info.getTableName().hasPrefix("Msg_")) {
            info.addZSTDNormalCompressField(
            WCDB::Field("message_content", nullptr));
            info.addZSTDNormalCompressField(WCDB::Field("source", nullptr));
        }
    });

    if (!database.canOpen()) {
        throw std::runtime_error("WCDB cannot open database: " + databaseError(database));
    }

    if (schemaSummary) {
        printSchemaSummary(database);
        return 0;
    }

    const std::vector<std::string> tables = findMessageTables(database);
    int64_t messageRows = 0;
    for (const std::string& table : tables) {
        messageRows += countRows(database, table);
    }
    const DecompressionProbe probe
    = probeTransparentDecompression(database, tables);

    std::cout << "{\n"
              << "  \"opened\": true,\n"
              << "  \"read_only\": true,\n"
              << "  \"cipher_page_size\": " << kCipherPageSize << ",\n"
              << "  \"cipher_version\": 4,\n"
              << "  \"message_tables\": " << tables.size() << ",\n"
              << "  \"message_rows\": " << messageRows << ",\n"
              << "  \"compressed_sample_found\": "
              << (probe.found ? "true" : "false") << ",\n"
              << "  \"compressed_sample_returned_as_text\": "
              << (probe.returnedText ? "true" : "false") << ",\n"
              << "  \"compressed_sample_utf8_bytes\": " << probe.utf8Bytes << "\n"
              << "}\n";

    return probe.found && !probe.returnedText ? 2 : 0;
}

} // namespace

int main(int argc, char* argv[])
{
    const bool schemaSummary
    = argc == 4 && std::string_view(argv[3]) == "--schema-summary";
    if ((argc != 3 && argc != 4) || (argc == 4 && !schemaSummary)) {
        std::cerr << "Usage: weixin_wcdb_reader <message_0.db> "
                     "<key-record.json> [--schema-summary]\n";
        return 64;
    }

    try {
        return run(argv[1], argv[2], schemaSummary);
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
