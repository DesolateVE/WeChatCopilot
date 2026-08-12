#include "application.hpp"

#include "contact_resolver.hpp"
#include "database.hpp"
#include "export_writer.hpp"
#include "message_enricher.hpp"
#include "model.hpp"
#include "utility.hpp"

#include <WCDB/WCDBCpp.h>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace wechat::chat_exporter
{

int run(const Options& options)
{
    std::vector<unsigned char> key = loadKey(options);
    const KeyWiper keyWiper(key);

    ReadOnlyDatabase contactDb(options.databaseDirectory / "contact" / "contact.db", key);
    auto matches = findContacts(contactDb, options.query);
    std::optional<ContactMatch> fallback;
    if (matches.empty())
    {
        ReadOnlyDatabase messageDb(options.databaseDirectory / "message" / "message_0.db", key, true);
        const auto statement = WCDB::StatementSelect()
                                       .select({WCDB::Column::rowid(), column("user_name")})
                                       .from("Name2Id")
                                       .where(column("user_name") == WCDB::BindParameter() && column("is_session") != 0)
                                       .limit(1);
        messageDb.forEach(statement, {options.query},
                          [&](WCDB::Handle& handle)
                          {
                              ContactMatch match;
                              match.id = handle.getInteger(0);
                              match.username = textAt(handle, 1);
                              match.priority = 0;
                              fallback = std::move(match);
                          });
        if (!fallback.has_value())
        {
            throw std::runtime_error("no contact or message session matches the query");
        }
        matches.push_back(*fallback);
    }

    const ContactMatch contact = chooseContact(matches, options.query, options.selectedUsername);
    bool isGroup = endsWith(contact.username, "@chatroom");
    if (!isGroup && contactDb.tableExists("chat_room"))
    {
        contactDb.forEach(selectAllWhere("chat_room", "username"), {contact.username}, [&](WCDB::Handle&) { isGroup = true; });
    }

    ensureFreshOutputDirectory(options.outputDirectory);
    const std::string messageTable = "Msg_" + md5Hex(contact.username);
    const MessageEnricher enricher(options.databaseDirectory, key, contact.username);
    const auto augment = [&](nlohmann::json& message) { enricher.augment(message); };

    std::vector<ExportResult> exports;
    ReadOnlyDatabase messageDb(options.databaseDirectory / "message" / "message_0.db", key, true);
    exports.push_back(exportMessages(messageDb, "messages", contact.username, isGroup, messageTable, options.outputDirectory, augment));

    const auto bizPath = options.databaseDirectory / "message" / "biz_message_0.db";
    if (std::filesystem::is_regular_file(bizPath))
    {
        ReadOnlyDatabase bizDb(bizPath, key, true);
        if (bizDb.tableExists(messageTable))
        {
            exports.push_back(
                    exportMessages(bizDb, "biz_messages", contact.username, isGroup, messageTable, options.outputDirectory, augment));
        }
    }

    writeManifest(options, contact, isGroup, messageTable, exports);
    int64_t messageRows = 0;
    int64_t businessMessageRows = 0;
    for (const auto& item : exports)
    {
        if (item.name == "messages")
        {
            messageRows = item.rows;
        }
        else if (item.name == "biz_messages")
        {
            businessMessageRows = item.rows;
        }
    }
    const nlohmann::json summary{
            {"ok", true},
            {"conversation_kind", isGroup ? "group" : "user"},
            {"messages", messageRows},
            {"biz_messages", businessMessageRows},
            {"files", exports.size() + 1},
            {"output", pathUtf8(std::filesystem::absolute(options.outputDirectory))},
    };
    std::cout << summary.dump() << '\n';
    return 0;
}

} // namespace wechat::chat_exporter
