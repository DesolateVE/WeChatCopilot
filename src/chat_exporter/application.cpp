#include "application.hpp"

#include "contact_resolver.hpp"
#include "console_ui.hpp"
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
    std::cout << "WeChat 聊天记录导出器\n"
              << "数据库目录：" << pathUtf8(std::filesystem::absolute(options.databaseDirectory)) << "\n"
              << "正在从 http://127.0.0.1:6500/key/string 获取密钥...\n";

    std::vector<unsigned char> key = loadKeyFromPlugin();
    const KeyWiper keyWiper(key);
    std::cout << "密钥已获取，正在读取可导出会话...\n";

    ReadOnlyDatabase contactDb(options.databaseDirectory / "contact" / "contact.db", key);
    ReadOnlyDatabase messageDb(options.databaseDirectory / "message" / "message_0.db", key, true);
    const auto contacts = listExportableContacts(contactDb, messageDb);
    const std::optional<ContactMatch> selected = selectExportContact(contacts);
    if (!selected.has_value())
    {
        std::cout << "已取消导出。\n";
        return 0;
    }

    const ContactMatch& contact = *selected;
    bool isGroup = endsWith(contact.username, "@chatroom");
    if (!isGroup && contactDb.tableExists("chat_room"))
    {
        contactDb.forEach(selectAllWhere("chat_room", "username"), {contact.username}, [&](WCDB::Handle&) { isGroup = true; });
    }

    const std::filesystem::path outputDirectory =
            options.outputRootDirectory /
            std::filesystem::u8path("chat_export_" + md5Hex(contact.username) + "_" + timestamp());
    ensureFreshOutputDirectory(outputDirectory);
    std::cout << "\n开始导出到：" << pathUtf8(std::filesystem::absolute(outputDirectory)) << "\n";

    const std::string messageTable = "Msg_" + md5Hex(contact.username);
    const MessageEnricher enricher(options.databaseDirectory, key, contact.username);
    const auto augment = [&](nlohmann::json& message) { enricher.augment(message); };

    std::vector<ExportResult> exports;
    exports.push_back(exportMessages(messageDb, "messages", contact.username, isGroup, messageTable, outputDirectory, augment));

    const auto bizPath = options.databaseDirectory / "message" / "biz_message_0.db";
    if (std::filesystem::is_regular_file(bizPath))
    {
        ReadOnlyDatabase bizDb(bizPath, key, true);
        if (bizDb.tableExists(messageTable))
        {
            exports.push_back(
                    exportMessages(bizDb, "biz_messages", contact.username, isGroup, messageTable, outputDirectory, augment));
        }
    }

    writeManifest(outputDirectory, contact, isGroup, messageTable, exports);
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
            {"output", pathUtf8(std::filesystem::absolute(outputDirectory))},
    };
    std::cout << "导出完成：普通消息 " << messageRows << " 条，业务消息 "
              << businessMessageRows << " 条。\n";
    std::cout << summary.dump() << '\n';
    return 0;
}

} // namespace wechat::chat_exporter
