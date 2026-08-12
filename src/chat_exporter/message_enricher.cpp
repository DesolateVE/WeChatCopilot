#include "message_enricher.hpp"

#include "database.hpp"

#include <WCDB/WCDBCpp.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace wechat::chat_exporter
{
namespace
{

using Json = nlohmann::json;
using JsonItem = std::shared_ptr<const Json>;
using ItemsByKey = std::unordered_map<std::string, std::vector<JsonItem>>;

std::optional<int64_t> integer(const Json& value, const char* name)
{
    const auto found = value.find(name);
    if (found == value.end() || !found->is_number_integer())
    {
        return std::nullopt;
    }
    return found->get<int64_t>();
}

std::string exactKey(const int64_t localId, const int64_t serverId)
{
    return "E:" + std::to_string(localId) + ':' + std::to_string(serverId);
}

std::string localKey(const int64_t localId)
{
    return "L:" + std::to_string(localId);
}

std::string serverKey(const int64_t serverId)
{
    return "S:" + std::to_string(serverId);
}

void indexItem(ItemsByKey& items, const int64_t localId, const int64_t serverId, Json item)
{
    const auto shared = std::make_shared<const Json>(std::move(item));
    if (localId != 0 && serverId != 0)
    {
        items[exactKey(localId, serverId)].push_back(shared);
    }
    if (localId != 0)
    {
        items[localKey(localId)].push_back(shared);
    }
    if (serverId != 0)
    {
        items[serverKey(serverId)].push_back(shared);
    }
}

const std::vector<JsonItem>* findItems(const ItemsByKey& items, const int64_t localId, const int64_t serverId)
{
    if (localId != 0 && serverId != 0)
    {
        if (const auto found = items.find(exactKey(localId, serverId)); found != items.end())
        {
            return &found->second;
        }
    }
    if (localId != 0)
    {
        if (const auto found = items.find(localKey(localId)); found != items.end())
        {
            return &found->second;
        }
    }
    if (serverId != 0)
    {
        if (const auto found = items.find(serverKey(serverId)); found != items.end())
        {
            return &found->second;
        }
    }
    return nullptr;
}

std::string trim(std::string_view value)
{
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0)
    {
        value.remove_prefix(1);
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0)
    {
        value.remove_suffix(1);
    }
    return std::string(value);
}

std::string decodeXmlEntities(std::string value)
{
    const struct
    {
        std::string_view encoded;
        std::string_view decoded;
    } entities[] = {{"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&apos;", "'"}};
    for (const auto& [encoded, decoded] : entities)
    {
        size_t position = 0;
        while ((position = value.find(encoded, position)) != std::string::npos)
        {
            value.replace(position, encoded.size(), decoded);
            position += decoded.size();
        }
    }
    return value;
}

void addValue(Json& object, const std::string& name, Json value)
{
    const auto found = object.find(name);
    if (found == object.end())
    {
        object[name] = std::move(value);
        return;
    }
    if (!found->is_array())
    {
        Json values = Json::array();
        values.push_back(std::move(*found));
        *found = std::move(values);
    }
    found->push_back(std::move(value));
}

size_t tagEnd(const std::string_view text, const size_t start)
{
    char quote = '\0';
    for (size_t index = start; index < text.size(); ++index)
    {
        const char current = text[index];
        if (quote != '\0')
        {
            if (current == quote)
            {
                quote = '\0';
            }
        }
        else if (current == '\'' || current == '\"')
        {
            quote = current;
        }
        else if (current == '>')
        {
            return index;
        }
    }
    return std::string_view::npos;
}

Json parseAttributes(const std::string_view text, size_t position)
{
    Json attributes = Json::object();
    while (position < text.size())
    {
        while (position < text.size() && std::isspace(static_cast<unsigned char>(text[position])) != 0)
        {
            ++position;
        }
        if (position >= text.size() || text[position] == '/')
        {
            break;
        }
        const size_t nameStart = position;
        while (position < text.size() && text[position] != '=' && std::isspace(static_cast<unsigned char>(text[position])) == 0 &&
               text[position] != '/')
        {
            ++position;
        }
        const std::string name(text.substr(nameStart, position - nameStart));
        while (position < text.size() && std::isspace(static_cast<unsigned char>(text[position])) != 0)
        {
            ++position;
        }
        if (name.empty() || position >= text.size() || text[position] != '=')
        {
            while (position < text.size() && text[position] != ' ' && text[position] != '\t')
            {
                ++position;
            }
            continue;
        }
        ++position;
        while (position < text.size() && std::isspace(static_cast<unsigned char>(text[position])) != 0)
        {
            ++position;
        }
        if (position >= text.size())
        {
            break;
        }
        const char quote = text[position];
        std::string value;
        if (quote == '\'' || quote == '\"')
        {
            const size_t valueStart = ++position;
            const size_t valueEnd = text.find(quote, valueStart);
            if (valueEnd == std::string_view::npos)
            {
                break;
            }
            value = std::string(text.substr(valueStart, valueEnd - valueStart));
            position = valueEnd + 1;
        }
        else
        {
            const size_t valueStart = position;
            while (position < text.size() && std::isspace(static_cast<unsigned char>(text[position])) == 0 && text[position] != '/')
            {
                ++position;
            }
            value = std::string(text.substr(valueStart, position - valueStart));
        }
        attributes[name] = decodeXmlEntities(std::move(value));
    }
    return attributes;
}

std::optional<Json> parseXmlMetadata(const std::string& text)
{
    const size_t firstTag = text.find('<');
    if (firstTag == std::string::npos)
    {
        return std::nullopt;
    }
    Json result{{"format", "xml"}, {"elements", Json::object()}, {"fields", Json::object()}};
    const std::string prefix = trim(std::string_view(text).substr(0, firstTag));
    if (!prefix.empty())
    {
        result["prefix"] = prefix;
    }

    size_t cursor = firstTag;
    while ((cursor = text.find('<', cursor)) != std::string::npos)
    {
        if (cursor + 1 >= text.size() || text[cursor + 1] == '/' || text[cursor + 1] == '!' || text[cursor + 1] == '?')
        {
            ++cursor;
            continue;
        }
        size_t nameEnd = cursor + 1;
        while (nameEnd < text.size())
        {
            const char value = text[nameEnd];
            if (std::isalnum(static_cast<unsigned char>(value)) == 0 && value != '_' && value != '-' && value != ':' && value != '.')
            {
                break;
            }
            ++nameEnd;
        }
        if (nameEnd == cursor + 1)
        {
            ++cursor;
            continue;
        }
        const std::string name = text.substr(cursor + 1, nameEnd - cursor - 1);
        const size_t end = tagEnd(text, nameEnd);
        if (end == std::string::npos)
        {
            break;
        }
        Json attributes = parseAttributes(std::string_view(text).substr(nameEnd, end - nameEnd), 0);
        if (!attributes.empty())
        {
            addValue(result["elements"], name, std::move(attributes));
        }

        if (end == 0 || text[end - 1] != '/')
        {
            const std::string closing = "</" + name + ">";
            const size_t close = text.find(closing, end + 1);
            if (close != std::string::npos)
            {
                std::string_view body(text.data() + end + 1, close - end - 1);
                std::string value;
                constexpr std::string_view cdataStart = "<![CDATA[";
                constexpr std::string_view cdataEnd = "]]>";
                if (body.starts_with(cdataStart) && body.ends_with(cdataEnd))
                {
                    body.remove_prefix(cdataStart.size());
                    body.remove_suffix(cdataEnd.size());
                    value = trim(body);
                }
                else if (body.find('<') == std::string_view::npos)
                {
                    value = trim(body);
                }
                if (!value.empty())
                {
                    addValue(result["fields"], name, decodeXmlEntities(std::move(value)));
                }
            }
        }
        cursor = end + 1;
    }
    if (result["elements"].empty())
    {
        result.erase("elements");
    }
    if (result["fields"].empty())
    {
        result.erase("fields");
    }
    return result.size() > 1 ? std::optional<Json>(std::move(result)) : std::nullopt;
}

std::optional<Json> parseStructured(const std::string& text)
{
    const std::string normalized = trim(text);
    if (normalized.empty())
    {
        return std::nullopt;
    }
    if (normalized.front() == '{' || normalized.front() == '[')
    {
        Json parsed = Json::parse(normalized, nullptr, false);
        if (!parsed.is_discarded())
        {
            return Json{{"format", "json"}, {"value", std::move(parsed)}};
        }
    }
    return parseXmlMetadata(text);
}

std::string messageKind(const uint64_t type)
{
    switch (type)
    {
    case 1:
        return "text";
    case 3:
        return "image";
    case 34:
        return "voice";
    case 37:
        return "friend_request";
    case 42:
        return "contact_card";
    case 43:
        return "video";
    case 47:
        return "sticker";
    case 48:
        return "location";
    case 49:
        return "app";
    case 50:
        return "call";
    case 62:
        return "short_video";
    case 10000:
        return "system";
    case 10002:
        return "recall";
    default:
        return "unknown";
    }
}

std::unordered_set<std::string> md5Candidates(const Json& message)
{
    std::unordered_set<std::string> values;
    for (const char* field : {"message_content", "source"})
    {
        const auto found = message.find(field);
        if (found == message.end() || !found->is_string())
        {
            continue;
        }
        const std::string& text = found->get_ref<const std::string&>();
        for (size_t start = 0; start + 32 <= text.size(); ++start)
        {
            bool hexadecimal = true;
            for (size_t index = start; index < start + 32; ++index)
            {
                if (std::isxdigit(static_cast<unsigned char>(text[index])) == 0)
                {
                    hexadecimal = false;
                    break;
                }
            }
            if (!hexadecimal || (start != 0 && std::isxdigit(static_cast<unsigned char>(text[start - 1])) != 0) ||
                (start + 32 < text.size() && std::isxdigit(static_cast<unsigned char>(text[start + 32])) != 0))
            {
                continue;
            }
            std::string candidate = text.substr(start, 32);
            std::transform(candidate.begin(), candidate.end(), candidate.begin(),
                           [](const unsigned char value) { return static_cast<char>(std::tolower(value)); });
            values.insert(std::move(candidate));
            start += 31;
        }
    }
    return values;
}

void loadResources(const std::filesystem::path& path, const std::vector<unsigned char>& key, const std::string& username,
                   ItemsByKey& resources)
{
    if (!std::filesystem::is_regular_file(path))
    {
        return;
    }
    ReadOnlyDatabase database(path, key);
    if (!database.tableExists("MessageResourceInfo") || !database.tableExists("MessageResourceDetail") ||
        !database.tableExists("ChatName2Id") || !database.tableExists("SenderName2Id"))
    {
        return;
    }
    auto info = table("MessageResourceInfo", "i");
    auto detail = table("MessageResourceDetail", "d");
    auto chats = table("ChatName2Id", "c");
    auto senders = table("SenderName2Id", "s");
    auto join = WCDB::Join()
                        .table(info)
                        .join(chats)
                        .on(column("rowid", "c") == column("chat_id", "i"))
                        .leftJoin(senders)
                        .on(column("rowid", "s") == column("sender_id", "i"))
                        .leftJoin(detail)
                        .on(column("message_id", "d") == column("message_id", "i"));
    const auto statement = WCDB::StatementSelect()
                                   .select({
                                           column("message_id", "i"),
                                           column("message_local_type", "i"),
                                           column("message_create_time", "i"),
                                           column("message_local_id", "i"),
                                           column("message_svr_id", "i"),
                                           column("message_origin_source", "i"),
                                           column("packed_info", "i").as("message_packed_info"),
                                           column("user_name", "s").as("sender_username"),
                                           column("resource_id", "d"),
                                           column("type", "d").as("resource_type"),
                                           column("size", "d").as("resource_size"),
                                           column("create_time", "d").as("resource_create_time"),
                                           column("access_time", "d").as("resource_access_time"),
                                           column("status", "d").as("resource_status"),
                                           column("data_index", "d"),
                                           column("packed_info", "d").as("resource_packed_info"),
                                   })
                                   .from(join)
                                   .where(column("user_name", "c") == WCDB::BindParameter())
                                   .orders({column("message_create_time", "i").asAscOrder(), column("message_local_id", "i").asAscOrder(),
                                            column("resource_id", "d").asAscOrder()});

    std::unordered_map<int64_t, Json> bundles;
    database.forEach(statement, {username},
                     [&](WCDB::Handle& handle)
                     {
                         Json row = rowToJson(handle);
                         const auto messageId = integer(row, "message_id");
                         if (!messageId.has_value())
                         {
                             return;
                         }
                         auto [found, inserted] = bundles.try_emplace(*messageId);
                         Json& bundle = found->second;
                         if (inserted)
                         {
                             bundle = {{"source", "message_resource"},
                                       {"kind", "resource"},
                                       {"message_id", row["message_id"]},
                                       {"message_local_type", row["message_local_type"]},
                                       {"message_create_time", row["message_create_time"]},
                                       {"message_local_id", row["message_local_id"]},
                                       {"message_server_id", row["message_svr_id"]},
                                       {"message_origin_source", row["message_origin_source"]},
                                       {"packed_info", row["message_packed_info"]},
                                       {"sender_username", row["sender_username"]},
                                       {"details", Json::array()}};
                         }
                         if (!row["resource_id"].is_null())
                         {
                             bundle["details"].push_back({
                                     {"resource_id", row["resource_id"]},
                                     {"type", row["resource_type"]},
                                     {"size", row["resource_size"]},
                                     {"create_time", row["resource_create_time"]},
                                     {"access_time", row["resource_access_time"]},
                                     {"status", row["resource_status"]},
                                     {"data_index", row["data_index"]},
                                     {"packed_info", row["resource_packed_info"]},
                             });
                         }
                     });
    for (auto& entry : bundles)
    {
        Json& bundle = entry.second;
        const auto localId = integer(bundle, "message_local_id").value_or(0);
        const auto serverId = integer(bundle, "message_server_id").value_or(0);
        indexItem(resources, localId, serverId, std::move(bundle));
    }
}

void loadVoices(const std::filesystem::path& path, const std::vector<unsigned char>& key, const std::string& username, ItemsByKey& voices)
{
    if (!std::filesystem::is_regular_file(path))
    {
        return;
    }
    ReadOnlyDatabase database(path, key);
    if (!database.tableExists("VoiceInfo") || !database.tableExists("Name2Id"))
    {
        return;
    }
    auto voice = table("VoiceInfo", "v");
    auto names = table("Name2Id", "n");
    auto join = WCDB::Join().table(voice).join(names).on(column("rowid", "n") == column("chat_name_id", "v"));
    const auto statement = WCDB::StatementSelect()
                                   .select({column("local_id", "v"), column("svr_id", "v").as("server_id"), column("create_time", "v"),
                                            column("data_index", "v"), column("voice_data", "v").as("data")})
                                   .from(join)
                                   .where(column("user_name", "n") == WCDB::BindParameter())
                                   .orders({column("create_time", "v").asAscOrder(), column("local_id", "v").asAscOrder()});
    database.forEach(statement, {username},
                     [&](WCDB::Handle& handle)
                     {
                         Json row = rowToJson(handle);
                         const auto localId = integer(row, "local_id").value_or(0);
                         const auto serverId = integer(row, "server_id").value_or(0);
                         row["source"] = "media_0";
                         row["kind"] = "voice";
                         indexItem(voices, localId, serverId, std::move(row));
                     });
}

void loadHardlinks(const std::filesystem::path& path, const std::vector<unsigned char>& key, const std::string& username,
                   ItemsByKey& hardlinks)
{
    if (!std::filesystem::is_regular_file(path))
    {
        return;
    }
    ReadOnlyDatabase database(path, key);
    if (!database.tableExists("dir2id"))
    {
        return;
    }
    for (const std::string category : {"image", "video", "file"})
    {
        const std::string tableName = category + "_hardlink_info_v4";
        if (!database.tableExists(tableName))
        {
            continue;
        }
        auto item = table(tableName.c_str(), "h");
        auto names = table("dir2id", "d");
        auto join = WCDB::Join().table(item).join(names).on(column("rowid", "d") == column("dir2", "h"));
        const auto statement = WCDB::StatementSelect()
                                       .select({column("md5", "h"), column("type", "h"), column("file_name", "h"), column("file_size", "h"),
                                                column("modify_time", "h"), column("extra_buffer", "h")})
                                       .from(join)
                                       .where(column("username", "d") == WCDB::BindParameter())
                                       .orders({column("modify_time", "h").asAscOrder(), column("_rowid_", "h").asAscOrder()});
        database.forEach(statement, {username},
                         [&](WCDB::Handle& handle)
                         {
                             Json row = rowToJson(handle);
                             if (!row["md5"].is_string())
                             {
                                 return;
                             }
                             std::string digest = row["md5"].get<std::string>();
                             std::transform(digest.begin(), digest.end(), digest.begin(),
                                            [](const unsigned char value) { return static_cast<char>(std::tolower(value)); });
                             row["source"] = "hardlink";
                             row["kind"] = category;
                             hardlinks[digest].push_back(std::make_shared<const Json>(std::move(row)));
                         });
    }
}

} // namespace

MessageEnricher::MessageEnricher(const std::filesystem::path& databaseDirectory, const std::vector<unsigned char>& key,
                                 const std::string& username)
{
    loadResources(databaseDirectory / "message" / "message_resource.db", key, username, resources_);
    loadVoices(databaseDirectory / "message" / "media_0.db", key, username, voices_);
    loadHardlinks(databaseDirectory / "hardlink" / "hardlink.db", key, username, hardlinksByMd5_);
}

void MessageEnricher::augment(Json& message) const
{
    const auto localId = integer(message, "local_id").value_or(0);
    const auto serverId = integer(message, "server_id").value_or(0);
    const auto type = integer(message, "local_type_base").value_or(0);
    message["message_kind"] = messageKind(static_cast<uint64_t>(type));

    for (const auto& [field, target] : {std::pair{"message_content", "parsed_content"}, std::pair{"source", "parsed_source"}})
    {
        const auto found = message.find(field);
        if (found != message.end() && found->is_string())
        {
            if (auto parsed = parseStructured(found->get<std::string>()); parsed.has_value())
            {
                message[target] = std::move(*parsed);
            }
        }
    }

    Json attachments = Json::array();
    if (const auto* resources = findItems(resources_, localId, serverId))
    {
        for (const auto& resource : *resources)
        {
            attachments.push_back(*resource);
        }
    }
    if (const auto* voices = findItems(voices_, localId, serverId))
    {
        for (const auto& voice : *voices)
        {
            attachments.push_back(*voice);
        }
    }
    std::unordered_set<std::string> attachedFiles;
    for (const std::string& digest : md5Candidates(message))
    {
        if (const auto found = hardlinksByMd5_.find(digest); found != hardlinksByMd5_.end())
        {
            for (const auto& hardlink : found->second)
            {
                const std::string identity = hardlink->dump();
                if (attachedFiles.insert(identity).second)
                {
                    attachments.push_back(*hardlink);
                }
            }
        }
    }
    if (!attachments.empty())
    {
        message["attachments"] = std::move(attachments);
    }
}

} // namespace wechat::chat_exporter
