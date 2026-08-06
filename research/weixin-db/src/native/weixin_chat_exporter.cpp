#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>

#include <sqlite3.h>
#define ZSTD_STATIC_LINKING_ONLY
#include <zstd.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

constexpr int kCipherPageSize = 4096;
constexpr size_t kMaximumDecompressedBytes = 512ULL * 1024ULL * 1024ULL;

std::string wideToUtf8(const std::wstring_view value)
{
    if (value.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(
    CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
    nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        throw std::runtime_error("cannot convert command line to UTF-8");
    }
    std::string converted(static_cast<size_t>(size), '\0');
    if (WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        converted.data(), size, nullptr, nullptr)
        != size) {
        throw std::runtime_error("cannot convert command line to UTF-8");
    }
    return converted;
}

std::string pathUtf8(const std::filesystem::path& path)
{
    const auto native = path.u8string();
    return std::string(native.begin(), native.end());
}

std::string readFile(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open file: " + pathUtf8(path));
    }
    return std::string(
    std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

class KeyBytes final {
public:
    explicit KeyBytes(std::vector<unsigned char> bytes)
    : m_bytes(std::move(bytes))
    {
    }

    KeyBytes(const KeyBytes&) = delete;
    KeyBytes& operator=(const KeyBytes&) = delete;

    ~KeyBytes()
    {
        SecureZeroMemory(m_bytes.data(), m_bytes.size());
    }

    const unsigned char* data() const { return m_bytes.data(); }
    int size() const { return static_cast<int>(m_bytes.size()); }

private:
    std::vector<unsigned char> m_bytes;
};

KeyBytes loadKeyRecord(const std::filesystem::path& path)
{
    const nlohmann::json document = nlohmann::json::parse(readFile(path));
    if (!document.contains("key_hex") || !document.at("key_hex").is_string()) {
        throw std::runtime_error("key record has no string key_hex field");
    }
    const std::string hex = document.at("key_hex").get<std::string>();
    if (hex.size() != 64) {
        throw std::runtime_error("expected a 32-byte WCDB key");
    }
    DWORD byteCount = 0;
    if (CryptStringToBinaryA(
        hex.data(), static_cast<DWORD>(hex.size()), CRYPT_STRING_HEXRAW,
        nullptr, &byteCount, nullptr, nullptr)
        == FALSE) {
        throw std::runtime_error("key_hex is not valid hexadecimal");
    }
    std::vector<unsigned char> bytes(byteCount);
    if (CryptStringToBinaryA(
        hex.data(), static_cast<DWORD>(hex.size()), CRYPT_STRING_HEXRAW,
        bytes.data(), &byteCount, nullptr, nullptr)
        == FALSE) {
        throw std::runtime_error("cannot decode key_hex");
    }
    bytes.resize(byteCount);
    if (bytes.size() != 32) {
        throw std::runtime_error("expected a 32-byte WCDB key");
    }
    return KeyBytes(std::move(bytes));
}

std::string base64Encode(const unsigned char* data, const size_t size)
{
    if (size == 0) {
        return {};
    }
    if (size > MAXDWORD) {
        throw std::runtime_error("BLOB is too large for Base64 encoding");
    }
    DWORD characterCount = 0;
    constexpr DWORD flags = CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF;
    if (CryptBinaryToStringA(
        data, static_cast<DWORD>(size), flags, nullptr, &characterCount)
        == FALSE) {
        throw std::runtime_error("cannot determine Base64 output size");
    }
    std::string output(characterCount, '\0');
    if (CryptBinaryToStringA(
        data, static_cast<DWORD>(size), flags, output.data(), &characterCount)
        == FALSE) {
        throw std::runtime_error("cannot encode BLOB as Base64");
    }
    output.resize(characterCount);
    if (!output.empty() && output.back() == '\0') {
        output.pop_back();
    }
    return output;
}

std::string quoteIdentifier(const std::string_view identifier)
{
    std::string quoted = "\"";
    for (const char character : identifier) {
        if (character == '"') {
            quoted += "\"\"";
        } else {
            quoted += character;
        }
    }
    quoted += '"';
    return quoted;
}

std::string md5Hex(const std::string_view value)
{
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectSize = 0;
    DWORD resultSize = 0;
    std::vector<unsigned char> object;
    std::array<unsigned char, 16> digest{};

    auto check = [](const NTSTATUS status, const char* operation) {
        if (status < 0) {
            throw std::runtime_error(std::string("BCrypt failure: ") + operation);
        }
    };

    try {
        check(BCryptOpenAlgorithmProvider(
              &algorithm, BCRYPT_MD5_ALGORITHM, nullptr, 0),
              "open MD5 provider");
        check(BCryptGetProperty(
              algorithm, BCRYPT_OBJECT_LENGTH,
              reinterpret_cast<PUCHAR>(&objectSize), sizeof(objectSize),
              &resultSize, 0),
              "get MD5 object size");
        object.resize(objectSize);
        check(BCryptCreateHash(
              algorithm, &hash, object.data(), objectSize, nullptr, 0, 0),
              "create MD5 hash");
        check(BCryptHashData(
              hash,
              reinterpret_cast<PUCHAR>(const_cast<char*>(value.data())),
              static_cast<ULONG>(value.size()), 0),
              "hash username");
        check(BCryptFinishHash(
              hash, digest.data(), static_cast<ULONG>(digest.size()), 0),
              "finish MD5 hash");
    } catch (...) {
        if (hash != nullptr) {
            BCryptDestroyHash(hash);
        }
        if (algorithm != nullptr) {
            BCryptCloseAlgorithmProvider(algorithm, 0);
        }
        throw;
    }
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);

    std::ostringstream stream;
    stream << std::hex << std::setfill('0');
    for (const unsigned char byte : digest) {
        stream << std::setw(2) << static_cast<unsigned int>(byte);
    }
    return stream.str();
}

class Statement final {
public:
    Statement(sqlite3* database, const std::string& sql)
    : m_database(database)
    {
        const int result = sqlite3_prepare_v2(
        database, sql.c_str(), static_cast<int>(sql.size()), &m_statement, nullptr);
        if (result != SQLITE_OK) {
            throw std::runtime_error(
            "SQL prepare failed: " + std::string(sqlite3_errmsg(database))
            + " | " + sql);
        }
    }

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    ~Statement()
    {
        sqlite3_finalize(m_statement);
    }

    sqlite3_stmt* get() const { return m_statement; }

    void bindText(const int index, const std::string& value)
    {
        if (sqlite3_bind_text(
            m_statement, index, value.data(), static_cast<int>(value.size()),
            SQLITE_TRANSIENT)
            != SQLITE_OK) {
            throw std::runtime_error("cannot bind SQL text parameter");
        }
    }

    void bindInteger(const int index, const int64_t value)
    {
        if (sqlite3_bind_int64(m_statement, index, value) != SQLITE_OK) {
            throw std::runtime_error("cannot bind SQL integer parameter");
        }
    }

    bool step()
    {
        const int result = sqlite3_step(m_statement);
        if (result == SQLITE_ROW) {
            return true;
        }
        if (result == SQLITE_DONE) {
            return false;
        }
        throw std::runtime_error(
        "SQL step failed: " + std::string(sqlite3_errmsg(m_database)));
    }

private:
    sqlite3* m_database = nullptr;
    sqlite3_stmt* m_statement = nullptr;
};

class Database final {
public:
    Database(const std::filesystem::path& path, const KeyBytes& key)
    : m_path(path)
    {
        if (!std::filesystem::is_regular_file(path)) {
            throw std::runtime_error("database does not exist: " + pathUtf8(path));
        }
        const std::string utf8 = pathUtf8(std::filesystem::absolute(path));
        const int result = sqlite3_open_v2(
        utf8.c_str(), &m_database,
        SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX, nullptr);
        if (result != SQLITE_OK) {
            const std::string error
            = m_database == nullptr ? "unknown" : sqlite3_errmsg(m_database);
            close();
            throw std::runtime_error("cannot open database: " + error);
        }
        sqlite3_extended_result_codes(m_database, 1);
        sqlite3_busy_timeout(m_database, 5000);
        if (sqlite3_key(m_database, key.data(), key.size()) != SQLITE_OK) {
            throw std::runtime_error("sqlite3_key failed for " + pathUtf8(path));
        }
        execute("PRAGMA cipher_compatibility = 4");
        execute("PRAGMA cipher_page_size = 4096");
        execute("PRAGMA query_only = ON");
        Statement verify(m_database, "SELECT count(*) FROM sqlite_master");
        if (!verify.step()) {
            throw std::runtime_error("database verification returned no row");
        }
    }

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    ~Database() { close(); }

    sqlite3* get() const { return m_database; }
    const std::filesystem::path& path() const { return m_path; }

    bool tableExists(const std::string& table) const
    {
        Statement statement(
        m_database,
        "SELECT 1 FROM sqlite_master WHERE type='table' AND name=? LIMIT 1");
        statement.bindText(1, table);
        return statement.step();
    }

    std::optional<int64_t> rowIdFor(
    const std::string& table, const std::string& nameColumn,
    const std::string& value) const
    {
        const std::string sql
        = "SELECT rowid FROM " + quoteIdentifier(table) + " WHERE "
          + quoteIdentifier(nameColumn) + "=? LIMIT 1";
        Statement statement(m_database, sql);
        statement.bindText(1, value);
        if (!statement.step()) {
            return std::nullopt;
        }
        return sqlite3_column_int64(statement.get(), 0);
    }

private:
    void execute(const std::string& sql)
    {
        char* error = nullptr;
        const int result = sqlite3_exec(m_database, sql.c_str(), nullptr, nullptr, &error);
        if (result != SQLITE_OK) {
            const std::string description
            = error == nullptr ? sqlite3_errmsg(m_database) : error;
            sqlite3_free(error);
            throw std::runtime_error("SQL execution failed: " + description);
        }
    }

    void close()
    {
        if (m_database != nullptr) {
            sqlite3_close_v2(m_database);
            m_database = nullptr;
        }
    }

    std::filesystem::path m_path;
    sqlite3* m_database = nullptr;
};

using Binding = std::variant<int64_t, std::string>;

void bindAll(Statement& statement, const std::vector<Binding>& bindings)
{
    int index = 1;
    for (const Binding& binding : bindings) {
        if (std::holds_alternative<int64_t>(binding)) {
            statement.bindInteger(index, std::get<int64_t>(binding));
        } else {
            statement.bindText(index, std::get<std::string>(binding));
        }
        ++index;
    }
}

struct Cell {
    int type = SQLITE_NULL;
    int64_t integer = 0;
    double floating = 0.0;
    std::string text;
    std::vector<unsigned char> blob;
};

Cell readCell(sqlite3_stmt* statement, const int index)
{
    Cell cell;
    cell.type = sqlite3_column_type(statement, index);
    switch (cell.type) {
    case SQLITE_INTEGER:
        cell.integer = sqlite3_column_int64(statement, index);
        break;
    case SQLITE_FLOAT:
        cell.floating = sqlite3_column_double(statement, index);
        break;
    case SQLITE_TEXT: {
        const auto* data = sqlite3_column_text(statement, index);
        const int size = sqlite3_column_bytes(statement, index);
        cell.text.assign(reinterpret_cast<const char*>(data), static_cast<size_t>(size));
        break;
    }
    case SQLITE_BLOB: {
        const auto* data = static_cast<const unsigned char*>(
        sqlite3_column_blob(statement, index));
        const int size = sqlite3_column_bytes(statement, index);
        if (size > 0) {
            cell.blob.assign(data, data + size);
        }
        break;
    }
    case SQLITE_NULL:
        break;
    default:
        throw std::runtime_error("unknown SQLite column type");
    }
    return cell;
}

std::string decompressZstd(const std::vector<unsigned char>& compressed)
{
    if (compressed.empty()) {
        return {};
    }
    unsigned long long expected
    = ZSTD_getFrameContentSize(compressed.data(), compressed.size());
    size_t outputSize = 0;
    if (expected == ZSTD_CONTENTSIZE_ERROR) {
        throw std::runtime_error("invalid Zstd frame in WCDB compressed column");
    }
    if (expected == ZSTD_CONTENTSIZE_UNKNOWN) {
        outputSize = ZSTD_decompressBound(compressed.data(), compressed.size());
    } else {
        if (expected > kMaximumDecompressedBytes) {
            throw std::runtime_error("compressed value exceeds safety limit");
        }
        outputSize = static_cast<size_t>(expected);
    }
    if (outputSize > kMaximumDecompressedBytes) {
        throw std::runtime_error("compressed value exceeds safety limit");
    }
    std::string output(outputSize, '\0');
    const size_t actual = ZSTD_decompress(
    output.data(), output.size(), compressed.data(), compressed.size());
    if (ZSTD_isError(actual) != 0) {
        throw std::runtime_error(
        "Zstd decompression failed: " + std::string(ZSTD_getErrorName(actual)));
    }
    output.resize(actual);
    return output;
}

nlohmann::json cellToJson(const Cell& cell)
{
    switch (cell.type) {
    case SQLITE_NULL:
        return nullptr;
    case SQLITE_INTEGER:
        return cell.integer;
    case SQLITE_FLOAT:
        return cell.floating;
    case SQLITE_TEXT:
        return cell.text;
    case SQLITE_BLOB:
        return nlohmann::json{
            { "encoding", "base64" },
            { "bytes", cell.blob.size() },
            { "data", base64Encode(cell.blob.data(), cell.blob.size()) },
        };
    default:
        throw std::runtime_error("cannot serialize SQLite value");
    }
}

std::string dumpJson(const nlohmann::json& value, const int indent = -1)
{
    return value.dump(
    indent, ' ', false, nlohmann::json::error_handler_t::replace);
}

std::string isoUtc(const int64_t timestamp)
{
    if (timestamp <= 0) {
        return {};
    }
    const std::time_t value = static_cast<std::time_t>(timestamp);
    std::tm calendar{};
    if (gmtime_s(&calendar, &value) != 0) {
        return {};
    }
    std::ostringstream stream;
    stream << std::put_time(&calendar, "%Y-%m-%dT%H:%M:%SZ");
    return stream.str();
}

struct ExportResult {
    std::string name;
    std::filesystem::path file;
    int64_t rows = 0;
};

ExportResult exportQuery(
Database& database, const std::string& name, const std::string& sql,
const std::vector<Binding>& bindings, const std::filesystem::path& outputPath)
{
    if (std::filesystem::exists(outputPath)) {
        throw std::runtime_error("refusing to overwrite: " + pathUtf8(outputPath));
    }
    std::ofstream output(outputPath, std::ios::binary);
    if (!output) {
        throw std::runtime_error("cannot create output: " + pathUtf8(outputPath));
    }
    Statement statement(database.get(), sql);
    bindAll(statement, bindings);
    int64_t rows = 0;
    while (statement.step()) {
        sqlite3_stmt* raw = statement.get();
        const int columns = sqlite3_column_count(raw);
        nlohmann::json row = nlohmann::json::object();
        for (int index = 0; index < columns; ++index) {
            row[sqlite3_column_name(raw, index)]
            = cellToJson(readCell(raw, index));
        }
        output << dumpJson(row) << '\n';
        ++rows;
    }
    return ExportResult{ name, outputPath, rows };
}

ExportResult exportMessages(
Database& database, const std::string& exportName,
const std::string& conversationUsername, const bool isGroup,
const std::string& table, const std::filesystem::path& outputPath)
{
    if (!database.tableExists(table)) {
        std::ofstream empty(outputPath, std::ios::binary);
        return ExportResult{ exportName, outputPath, 0 };
    }
    const std::string sql
    = "SELECT m.*, sender.user_name AS real_sender_username FROM "
      + quoteIdentifier(table)
      + " AS m LEFT JOIN Name2Id AS sender ON sender.rowid=m.real_sender_id "
        "ORDER BY m.sort_seq, m.local_id";
    if (std::filesystem::exists(outputPath)) {
        throw std::runtime_error("refusing to overwrite: " + pathUtf8(outputPath));
    }
    std::ofstream output(outputPath, std::ios::binary);
    if (!output) {
        throw std::runtime_error("cannot create output: " + pathUtf8(outputPath));
    }
    Statement statement(database.get(), sql);
    int64_t rows = 0;
    while (statement.step()) {
        sqlite3_stmt* raw = statement.get();
        const int columnCount = sqlite3_column_count(raw);
        std::vector<std::string> names;
        std::vector<Cell> cells;
        names.reserve(static_cast<size_t>(columnCount));
        cells.reserve(static_cast<size_t>(columnCount));
        std::map<std::string, int> indexes;
        for (int index = 0; index < columnCount; ++index) {
            names.emplace_back(sqlite3_column_name(raw, index));
            cells.emplace_back(readCell(raw, index));
            indexes[names.back()] = index;
        }

        const auto decompressColumn = [&](const char* valueName, const char* flagName) {
            const auto valueIt = indexes.find(valueName);
            const auto flagIt = indexes.find(flagName);
            if (valueIt == indexes.end() || flagIt == indexes.end()) {
                return;
            }
            Cell& value = cells[static_cast<size_t>(valueIt->second)];
            const Cell& flag = cells[static_cast<size_t>(flagIt->second)];
            if (flag.type == SQLITE_INTEGER && flag.integer == 4
                && value.type == SQLITE_BLOB) {
                value.text = decompressZstd(value.blob);
                value.blob.clear();
                value.type = SQLITE_TEXT;
            }
        };
        decompressColumn("message_content", "WCDB_CT_message_content");
        decompressColumn("source", "WCDB_CT_source");

        int64_t localType = 0;
        const auto typeIt = indexes.find("local_type");
        if (typeIt != indexes.end()
            && cells[static_cast<size_t>(typeIt->second)].type == SQLITE_INTEGER) {
            localType = cells[static_cast<size_t>(typeIt->second)].integer;
        }
        int64_t createTime = 0;
        const auto timeIt = indexes.find("create_time");
        if (timeIt != indexes.end()
            && cells[static_cast<size_t>(timeIt->second)].type == SQLITE_INTEGER) {
            createTime = cells[static_cast<size_t>(timeIt->second)].integer;
        }

        nlohmann::json row = nlohmann::json::object();
        row["conversation_username"] = conversationUsername;
        row["conversation_kind"] = isGroup ? "group" : "user";
        row["local_type_base"]
        = static_cast<uint64_t>(localType) & 0xFFFFFFFFULL;
        row["local_type_flags"] = static_cast<uint64_t>(localType) >> 32;
        const std::string timestamp = isoUtc(createTime);
        if (timestamp.empty()) {
            row["create_time_utc"] = nullptr;
        } else {
            row["create_time_utc"] = timestamp;
        }
        for (int index = 0; index < columnCount; ++index) {
            row[names[static_cast<size_t>(index)]]
            = cellToJson(cells[static_cast<size_t>(index)]);
        }
        output << dumpJson(row) << '\n';
        ++rows;
    }
    return ExportResult{ exportName, outputPath, rows };
}

struct ContactMatch {
    int64_t id = 0;
    std::string username;
    std::string alias;
    std::string remark;
    std::string nickname;
    int score = 99;
};

std::optional<ContactMatch> resolveContact(Database& database, const std::string& input)
{
    Statement statement(
    database.get(),
    "SELECT id,username,alias,remark,nick_name FROM contact "
    "WHERE username=? OR alias=? OR remark=? OR nick_name=?");
    for (int index = 1; index <= 4; ++index) {
        statement.bindText(index, input);
    }
    std::vector<ContactMatch> matches;
    while (statement.step()) {
        sqlite3_stmt* raw = statement.get();
        ContactMatch match;
        match.id = sqlite3_column_int64(raw, 0);
        const auto text = [&](const int column) {
            const auto* value = sqlite3_column_text(raw, column);
            const int size = sqlite3_column_bytes(raw, column);
            return value == nullptr
                       ? std::string()
                       : std::string(
                         reinterpret_cast<const char*>(value),
                         static_cast<size_t>(size));
        };
        match.username = text(1);
        match.alias = text(2);
        match.remark = text(3);
        match.nickname = text(4);
        if (match.username == input) {
            match.score = 0;
        } else if (match.alias == input) {
            match.score = 1;
        } else if (match.remark == input) {
            match.score = 2;
        } else {
            match.score = 3;
        }
        matches.emplace_back(std::move(match));
    }
    if (matches.empty()) {
        return std::nullopt;
    }
    std::sort(matches.begin(), matches.end(), [](const auto& left, const auto& right) {
        if (left.score != right.score) {
            return left.score < right.score;
        }
        return left.username < right.username;
    });
    if (matches.size() > 1 && matches[0].score == matches[1].score) {
        throw std::runtime_error(
        "input matches multiple contacts at the same priority; use the internal username");
    }
    return matches.front();
}

bool endsWith(const std::string& value, const std::string_view suffix)
{
    return value.size() >= suffix.size()
           && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

struct Options {
    std::filesystem::path snapshotRoot;
    std::filesystem::path keyRecord;
    std::string input;
    std::filesystem::path outputDirectory;
};

std::string pathTimestamp()
{
    const std::time_t now = std::time(nullptr);
    std::tm calendar{};
    if (localtime_s(&calendar, &now) != 0) {
        throw std::runtime_error("cannot create output timestamp");
    }
    std::ostringstream stream;
    stream << std::put_time(&calendar, "%Y%m%d_%H%M%S");
    return stream.str();
}

Options parseOptions(const int argc, wchar_t* argv[])
{
    if (argc == 2) {
        Options options;
        options.input = wideToUtf8(argv[1]);
        const std::filesystem::path root = std::filesystem::current_path();
        options.snapshotRoot
        = root / "artifacts" / "exports" / "full_db_snapshot_20260731_final";
        options.keyRecord
        = root / "artifacts" / "exports" / "wcdb_cipher_key_message_0.json";
        options.outputDirectory
        = root / "artifacts" / "exports"
          / std::filesystem::u8path(
            "chat_export_" + md5Hex(options.input) + "_" + pathTimestamp());
        return options;
    }
    if (argc != 9) {
        throw std::runtime_error(
        "usage: weixin_chat_exporter <wechat-id> | "
        "weixin_chat_exporter --snapshot <snapshot-root> --key-record "
        "<key.json> --wechat-id <id> --output <directory>");
    }
    Options options;
    for (int index = 1; index < argc; index += 2) {
        const std::wstring name = argv[index];
        const std::wstring value = argv[index + 1];
        if (name == L"--snapshot") {
            options.snapshotRoot = value;
        } else if (name == L"--key-record") {
            options.keyRecord = value;
        } else if (name == L"--wechat-id") {
            options.input = wideToUtf8(value);
        } else if (name == L"--output") {
            options.outputDirectory = value;
        } else {
            throw std::runtime_error("unknown option: " + wideToUtf8(name));
        }
    }
    if (options.snapshotRoot.empty() || options.keyRecord.empty()
        || options.input.empty() || options.outputDirectory.empty()) {
        throw std::runtime_error("all command line options are required");
    }
    return options;
}

std::filesystem::path locateStorageRoot(const std::filesystem::path& snapshotRoot)
{
    if (std::filesystem::is_directory(snapshotRoot / "db_storage")) {
        return snapshotRoot / "db_storage";
    }
    if (std::filesystem::is_directory(snapshotRoot / "message")
        && std::filesystem::is_directory(snapshotRoot / "contact")) {
        return snapshotRoot;
    }
    throw std::runtime_error(
    "snapshot root must contain db_storage or be db_storage itself");
}

std::filesystem::path outputFile(
const std::filesystem::path& directory, const std::string_view name)
{
    return directory / std::filesystem::u8path(std::string(name));
}

void ensureFreshOutputDirectory(const std::filesystem::path& directory)
{
    if (std::filesystem::exists(directory)) {
        if (!std::filesystem::is_directory(directory)) {
            throw std::runtime_error("output path is not a directory");
        }
        if (!std::filesystem::is_empty(directory)) {
            throw std::runtime_error("output directory must be empty");
        }
    } else if (!std::filesystem::create_directories(directory)) {
        throw std::runtime_error("cannot create output directory");
    }
}

void writeManifest(
const Options& options, const std::filesystem::path& storageRoot,
const ContactMatch& contact, const bool isGroup, const std::string& messageTable,
const std::vector<ExportResult>& exports)
{
    const std::filesystem::path path
    = outputFile(options.outputDirectory, "manifest.json");
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("cannot create manifest");
    }
    int64_t totalRows = 0;
    for (const ExportResult& item : exports) {
        totalRows += item.rows;
    }
    nlohmann::json manifest{
        { "format_version", 1 },
        { "read_only", true },
        { "snapshot_root", pathUtf8(storageRoot) },
        { "input", options.input },
        { "resolved_username", contact.username },
        { "conversation_kind", isGroup ? "group" : "user" },
        { "message_table", messageTable },
        { "total_exported_rows", totalRows },
        { "files", nlohmann::json::array() },
    };
    for (const ExportResult& item : exports) {
        manifest["files"].push_back({
            { "name", item.name },
            { "file", pathUtf8(item.file.filename()) },
            { "rows", item.rows },
        });
    }
    output << dumpJson(manifest, 2) << '\n';
}

int run(const Options& options)
{
    const std::filesystem::path storageRoot = locateStorageRoot(options.snapshotRoot);
    ensureFreshOutputDirectory(options.outputDirectory);
    KeyBytes key = loadKeyRecord(options.keyRecord);

    Database contactDb(storageRoot / "contact" / "contact.db", key);
    Database messageDb(storageRoot / "message" / "message_0.db", key);
    std::optional<ContactMatch> resolved = resolveContact(contactDb, options.input);
    if (!resolved.has_value()) {
        Statement statement(
        messageDb.get(),
        "SELECT rowid,user_name FROM Name2Id WHERE user_name=? AND is_session<>0");
        statement.bindText(1, options.input);
        if (!statement.step()) {
            throw std::runtime_error("no contact or message session matches the input");
        }
        ContactMatch fallback;
        fallback.id = sqlite3_column_int64(statement.get(), 0);
        fallback.username = options.input;
        fallback.score = 0;
        resolved = fallback;
    }
    ContactMatch contact = *resolved;
    bool isGroup = endsWith(contact.username, "@chatroom");
    if (!isGroup) {
        Statement groupCheck(
        contactDb.get(), "SELECT 1 FROM chat_room WHERE username=? LIMIT 1");
        groupCheck.bindText(1, contact.username);
        isGroup = groupCheck.step();
    }

    const std::string digest = md5Hex(contact.username);
    const std::string messageTable = "Msg_" + digest;
    std::vector<ExportResult> exports;
    const auto add = [&](ExportResult result) { exports.emplace_back(std::move(result)); };

    add(exportQuery(
    contactDb, "contact", "SELECT * FROM contact WHERE username=?",
    { contact.username }, outputFile(options.outputDirectory, "contact.jsonl")));
    add(exportQuery(
    contactDb, "chat_room", "SELECT * FROM chat_room WHERE username=?",
    { contact.username }, outputFile(options.outputDirectory, "chat_room.jsonl")));
    add(exportQuery(
    contactDb, "chat_room_info",
    "SELECT * FROM chat_room_info_detail WHERE username_=?",
    { contact.username }, outputFile(options.outputDirectory, "chat_room_info.jsonl")));
    add(exportQuery(
    contactDb, "group_members",
    "SELECT room.username AS group_username,member.* "
    "FROM chat_room AS room JOIN chatroom_member AS cm ON cm.room_id=room.id "
    "JOIN contact AS member ON member.id=cm.member_id WHERE room.username=? "
    "ORDER BY member.remark,member.nick_name,member.username",
    { contact.username }, outputFile(options.outputDirectory, "group_members.jsonl")));

    add(exportMessages(
    messageDb, "messages", contact.username, isGroup, messageTable,
    outputFile(options.outputDirectory, "messages.jsonl")));
    if (messageDb.tableExists(messageTable)) {
        add(exportQuery(
        messageDb, "message_senders",
        "SELECT rowid,user_name,is_session FROM Name2Id WHERE rowid IN "
        "(SELECT DISTINCT real_sender_id FROM "
          + quoteIdentifier(messageTable) + ") ORDER BY rowid",
        {}, outputFile(options.outputDirectory, "message_senders.jsonl")));
    } else {
        std::ofstream empty(outputFile(options.outputDirectory, "message_senders.jsonl"));
        add(ExportResult{ "message_senders",
                          outputFile(options.outputDirectory, "message_senders.jsonl"), 0 });
    }

    Database bizMessageDb(storageRoot / "message" / "biz_message_0.db", key);
    add(exportMessages(
    bizMessageDb, "biz_messages", contact.username, isGroup, messageTable,
    outputFile(options.outputDirectory, "biz_messages.jsonl")));

    Database sessionDb(storageRoot / "session" / "session.db", key);
    add(exportQuery(
    sessionDb, "session", "SELECT * FROM SessionTable WHERE username=?",
    { contact.username }, outputFile(options.outputDirectory, "session.jsonl")));
    add(exportQuery(
    sessionDb, "session_unread",
    "SELECT u.*,n.user_name FROM SessionUnreadListTable_1 AS u "
    "JOIN Name2Id AS n ON n.rowid=u.username_id WHERE n.user_name=? "
    "ORDER BY u.create_time,u.server_id",
    { contact.username }, outputFile(options.outputDirectory, "session_unread.jsonl")));
    add(exportQuery(
    sessionDb, "session_unread_stat",
    "SELECT s.*,n.user_name FROM SessionUnreadStatTable_1 AS s "
    "JOIN Name2Id AS n ON n.rowid=s.username_id WHERE n.user_name=?",
    { contact.username },
    outputFile(options.outputDirectory, "session_unread_stat.jsonl")));
    add(exportQuery(
    sessionDb, "session_delete",
    "SELECT * FROM SessionDeleteTable WHERE username=?", { contact.username },
    outputFile(options.outputDirectory, "session_delete.jsonl")));
    add(exportQuery(
    sessionDb, "session_draft",
    "SELECT * FROM SessionDraft WHERE username=?", { contact.username },
    outputFile(options.outputDirectory, "session_draft.jsonl")));

    Database resourceDb(storageRoot / "message" / "message_resource.db", key);
    add(exportQuery(
    resourceDb, "resource_info",
    "SELECT i.*,c.user_name AS conversation_username,s.user_name AS sender_username "
    "FROM MessageResourceInfo AS i JOIN ChatName2Id AS c ON c.rowid=i.chat_id "
    "LEFT JOIN SenderName2Id AS s ON s.rowid=i.sender_id WHERE c.user_name=? "
    "ORDER BY i.message_create_time,i.message_local_id",
    { contact.username }, outputFile(options.outputDirectory, "resource_info.jsonl")));
    add(exportQuery(
    resourceDb, "resource_detail",
    "SELECT d.*,i.message_local_id,i.message_svr_id,c.user_name AS conversation_username "
    "FROM MessageResourceDetail AS d JOIN MessageResourceInfo AS i "
    "ON i.message_id=d.message_id JOIN ChatName2Id AS c ON c.rowid=i.chat_id "
    "WHERE c.user_name=? ORDER BY i.message_create_time,i.message_local_id,d.resource_id",
    { contact.username }, outputFile(options.outputDirectory, "resource_detail.jsonl")));
    add(exportQuery(
    resourceDb, "resource_fts_range",
    "SELECT r.*,c.user_name FROM FtsRange AS r JOIN ChatName2Id AS c "
    "ON c.rowid=r.session_id WHERE c.user_name=?",
    { contact.username }, outputFile(options.outputDirectory, "resource_fts_range.jsonl")));

    Database mediaDb(storageRoot / "message" / "media_0.db", key);
    add(exportQuery(
    mediaDb, "voice",
    "SELECT v.*,n.user_name AS conversation_username FROM VoiceInfo AS v "
    "JOIN Name2Id AS n ON n.rowid=v.chat_name_id WHERE n.user_name=? "
    "ORDER BY v.create_time,v.local_id",
    { contact.username }, outputFile(options.outputDirectory, "voice.jsonl")));

    Database ftsDb(storageRoot / "message" / "message_fts.db", key);
    for (int partition = 0; partition < 4; ++partition) {
        const std::string suffix = std::to_string(partition);
        const std::string table = "message_fts_v4_" + suffix + "_content";
        const std::string name = "fts_message_v4_" + suffix;
        add(exportQuery(
        ftsDb, name,
        "SELECT f.id AS fts_rowid,f.c0 AS acontent,f.c1 AS message_local_id,"
        "f.c2 AS sort_seq,f.c3 AS local_type,f.c4 AS session_id,"
        "f.c5 AS sender_id,f.c6 AS create_time,n.username AS conversation_username FROM "
          + quoteIdentifier(table)
          + " AS f JOIN name2id AS n ON n.rowid=f.c4 "
            "WHERE n.username=? ORDER BY CAST(f.c2 AS INTEGER),CAST(f.c1 AS INTEGER)",
        { contact.username },
        outputFile(options.outputDirectory, name + ".jsonl")));
    }
    add(exportQuery(
    ftsDb, "fts_range",
    "SELECT r.*,n.username AS conversation_username FROM message_fts_v4_range AS r "
    "JOIN name2id AS n ON n.rowid=r.session_id WHERE n.username=?",
    { contact.username }, outputFile(options.outputDirectory, "fts_range.jsonl")));

    Database generalDb(storageRoot / "general" / "general.db", key);
    const struct GeneralQuery {
        const char* name;
        const char* sql;
    } generalQueries[] = {
        { "revoked_messages", "SELECT * FROM revokemessage WHERE to_user_name=?" },
        { "revoked_batch_messages", "SELECT * FROM revokebatchmessage WHERE session_name=?" },
        { "transfers", "SELECT * FROM transferTable WHERE session_name=?" },
        { "red_envelopes", "SELECT * FROM redEnvelopeTable WHERE session_name=?" },
        { "group_payments", "SELECT * FROM groupPayTable WHERE session_name=?" },
        { "forward_recent", "SELECT * FROM ForwardRecent WHERE username=?" },
        { "search_recent", "SELECT * FROM SearchRecent WHERE username=?" },
        { "friend_messages", "SELECT * FROM FMessageTable WHERE user_name_=?" },
        { "wa_contact", "SELECT * FROM wacontact WHERE user_name=?" },
        { "voip", "SELECT * FROM ilink_voip WHERE wx_chatroom_=?" },
    };
    for (const GeneralQuery& query : generalQueries) {
        add(exportQuery(
        generalDb, query.name, query.sql, { contact.username },
        outputFile(options.outputDirectory, std::string(query.name) + ".jsonl")));
    }

    Database hardlinkDb(storageRoot / "hardlink" / "hardlink.db", key);
    for (const std::string type : { "image", "video", "file" }) {
        const std::string name = type + "_hardlinks";
        add(exportQuery(
        hardlinkDb, name,
        "SELECT h.*,d.username AS conversation_username FROM "
          + quoteIdentifier(type + "_hardlink_info_v4")
          + " AS h JOIN dir2id AS d ON d.rowid=h.dir2 WHERE d.username=? "
            "ORDER BY h.modify_time,h._rowid_",
        { contact.username }, outputFile(options.outputDirectory, name + ".jsonl")));
    }
    add(exportQuery(
    hardlinkDb, "hardlink_checkpoints",
    "SELECT c.*,d.username AS conversation_username FROM talker_checkpoint_v4 AS c "
    "JOIN dir2id AS d ON d.rowid=c.talker_id WHERE d.username=?",
    { contact.username },
    outputFile(options.outputDirectory, "hardlink_checkpoints.jsonl")));

    Database headImageDb(storageRoot / "head_image" / "head_image.db", key);
    add(exportQuery(
    headImageDb, "head_image", "SELECT * FROM head_image WHERE username=?",
    { contact.username }, outputFile(options.outputDirectory, "head_image.jsonl")));

    Database favoriteDb(storageRoot / "favorite" / "favorite.db", key);
    add(exportQuery(
    favoriteDb, "favorites",
    "SELECT f.*,src.user_name AS from_username,chat.user_name AS real_chat_username "
    "FROM fav_db_item AS f LEFT JOIN Name2Id AS src ON src.rowid=f.fromusr_id "
    "LEFT JOIN Name2Id AS chat ON chat.rowid=f.realchatname_id "
    "WHERE src.user_name=? OR chat.user_name=? ORDER BY f.update_time,f.local_id",
    { contact.username, contact.username },
    outputFile(options.outputDirectory, "favorites.jsonl")));

    Database solitaireDb(storageRoot / "solitaire" / "solitaire.db", key);
    for (const std::string prefix : { "Solitaire_", "SolitaireFold_", "SolitaireValid_" }) {
        const std::string table = prefix + digest;
        const std::string name = prefix.substr(0, prefix.size() - 1);
        const std::filesystem::path path
        = outputFile(options.outputDirectory, name + ".jsonl");
        if (solitaireDb.tableExists(table)) {
            add(exportQuery(
            solitaireDb, name, "SELECT * FROM " + quoteIdentifier(table), {}, path));
        } else {
            std::ofstream empty(path, std::ios::binary);
            add(ExportResult{ name, path, 0 });
        }
    }

    writeManifest(
    options, storageRoot, contact, isGroup, messageTable, exports);

    const auto findRows = [&](const std::string_view name) {
        const auto found = std::find_if(
        exports.begin(), exports.end(), [&](const ExportResult& item) {
            return item.name == name;
        });
        return found == exports.end() ? int64_t{ 0 } : found->rows;
    };
    const nlohmann::json summary{
        { "ok", true },
        { "conversation_kind", isGroup ? "group" : "user" },
        { "messages", findRows("messages") },
        { "biz_messages", findRows("biz_messages") },
        { "resource_info", findRows("resource_info") },
        { "voice", findRows("voice") },
        { "files", exports.size() + 1 },
        { "output", pathUtf8(std::filesystem::absolute(options.outputDirectory)) },
    };
    std::cout << dumpJson(summary) << '\n';
    return 0;
}

} // namespace

int wmain(const int argc, wchar_t* argv[])
{
    try {
        return run(parseOptions(argc, argv));
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
