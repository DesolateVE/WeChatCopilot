#include "application.hpp"
#include "contact_resolver.hpp"
#include "database.hpp"
#include "export_writer.hpp"
#include "model.hpp"
#include "utility.hpp"

#include <WCDB/WCDBCpp.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace wechat::chat_exporter {

int run(const Options &options) {
  std::vector<unsigned char> key = loadKey(options);
  const KeyWiper keyWiper(key);
  ReadOnlyDatabase contactDb(
      options.databaseDirectory / "contact" / "contact.db", key);
  auto matches = findContacts(contactDb, options.query);

  std::optional<ContactMatch> fallback;
  if (matches.empty()) {
    ReadOnlyDatabase messageDb(
        options.databaseDirectory / "message" / "message_0.db", key, true);
    const auto statement =
        WCDB::StatementSelect()
            .select({WCDB::Column::rowid(), column("user_name")})
            .from("Name2Id")
            .where(column("user_name") == WCDB::BindParameter() &&
                   column("is_session") != 0)
            .limit(1);
    messageDb.forEach(statement, {options.query}, [&](WCDB::Handle &handle) {
      ContactMatch match;
      match.id = handle.getInteger(0);
      match.username = textAt(handle, 1);
      match.priority = 0;
      fallback = std::move(match);
    });
    if (!fallback.has_value()) {
      throw std::runtime_error(
          "no contact or message session matches the query");
    }
    matches.push_back(*fallback);
  }

  const ContactMatch contact =
      chooseContact(matches, options.query, options.selectedUsername);
  bool isGroup = endsWith(contact.username, "@chatroom");
  if (!isGroup && contactDb.tableExists("chat_room")) {
    bool found = false;
    contactDb.forEach(selectAllWhere("chat_room", "username"),
                      {contact.username},
                      [&](WCDB::Handle &) { found = true; });
    isGroup = found;
  }

  ensureFreshOutputDirectory(options.outputDirectory);
  const std::string digest = md5Hex(contact.username);
  const std::string messageTable = "Msg_" + digest;
  std::vector<ExportResult> exports;
  const auto add = [&](ExportResult result) {
    exports.emplace_back(std::move(result));
  };

  add(exportQuery(contactDb, "contact", selectAllWhere("contact", "username"),
                  {contact.username}, options.outputDirectory));
  add(exportQuery(contactDb, "chat_room",
                  selectAllWhere("chat_room", "username"), {contact.username},
                  options.outputDirectory));
  if (contactDb.tableExists("chat_room_info_detail")) {
    add(exportQuery(contactDb, "chat_room_info",
                    selectAllWhere("chat_room_info_detail", "username_"),
                    {contact.username}, options.outputDirectory));
  } else {
    add(createEmptyExport("chat_room_info", options.outputDirectory));
  }

  if (contactDb.tableExists("chatroom_member")) {
    auto room = table("chat_room", "room");
    auto memberMap = table("chatroom_member", "cm");
    auto member = table("contact", "member");
    auto join = WCDB::Join()
                    .table(room)
                    .join(memberMap)
                    .on(column("room_id", "cm") == column("id", "room"))
                    .join(member)
                    .on(column("id", "member") == column("member_id", "cm"));
    const auto statement =
        WCDB::StatementSelect()
            .select({column("username", "room").as("group_username"),
                     WCDB::Column::all().table("member")})
            .from(join)
            .where(column("username", "room") == WCDB::BindParameter())
            .orders({column("remark", "member").asAscOrder(),
                     column("nick_name", "member").asAscOrder(),
                     column("username", "member").asAscOrder()});
    add(exportQuery(contactDb, "group_members", statement, {contact.username},
                    options.outputDirectory));
  } else {
    add(createEmptyExport("group_members", options.outputDirectory));
  }

  ReadOnlyDatabase messageDb(
      options.databaseDirectory / "message" / "message_0.db", key, true);
  add(exportMessages(messageDb, "messages", contact.username, isGroup,
                     messageTable, options.outputDirectory));
  if (messageDb.tableExists(messageTable)) {
    const auto senderIds = WCDB::StatementSelect()
                               .select(column("real_sender_id"))
                               .distinct()
                               .from(messageTable);
    const auto senderStatement =
        WCDB::StatementSelect()
            .select({WCDB::Column::rowid(), column("user_name"),
                     column("is_session")})
            .from("Name2Id")
            .where(WCDB::Column::rowid().in(senderIds))
            .order(WCDB::Column::rowid().asAscOrder());
    add(exportQuery(messageDb, "message_senders", senderStatement, {},
                    options.outputDirectory));
  } else {
    add(createEmptyExport("message_senders", options.outputDirectory));
  }

  const auto bizPath =
      options.databaseDirectory / "message" / "biz_message_0.db";
  if (std::filesystem::is_regular_file(bizPath)) {
    ReadOnlyDatabase bizDb(bizPath, key, true);
    add(exportMessages(bizDb, "biz_messages", contact.username, isGroup,
                       messageTable, options.outputDirectory));
  } else {
    add(createEmptyExport("biz_messages", options.outputDirectory));
  }

  const auto sessionPath = options.databaseDirectory / "session" / "session.db";
  if (std::filesystem::is_regular_file(sessionPath)) {
    ReadOnlyDatabase sessionDb(sessionPath, key);
    add(exportQuery(sessionDb, "session",
                    selectAllWhere("SessionTable", "username"),
                    {contact.username}, options.outputDirectory));
    auto unread = table("SessionUnreadListTable_1", "u");
    auto names = table("Name2Id", "n");
    auto unreadJoin = WCDB::Join().table(unread).join(names).on(
        column("rowid", "n") == column("username_id", "u"));
    const auto unreadStatement =
        WCDB::StatementSelect()
            .select({WCDB::Column::all().table("u"), column("user_name", "n")})
            .from(unreadJoin)
            .where(column("user_name", "n") == WCDB::BindParameter())
            .orders({column("create_time", "u").asAscOrder(),
                     column("server_id", "u").asAscOrder()});
    add(exportQuery(sessionDb, "session_unread", unreadStatement,
                    {contact.username}, options.outputDirectory));

    auto unreadStat = table("SessionUnreadStatTable_1", "s");
    auto statJoin = WCDB::Join()
                        .table(unreadStat)
                        .join(names)
                        .on(column("rowid", "n") == column("username_id", "s"));
    const auto statStatement =
        WCDB::StatementSelect()
            .select({WCDB::Column::all().table("s"), column("user_name", "n")})
            .from(statJoin)
            .where(column("user_name", "n") == WCDB::BindParameter());
    add(exportQuery(sessionDb, "session_unread_stat", statStatement,
                    {contact.username}, options.outputDirectory));
    add(exportQuery(sessionDb, "session_delete",
                    selectAllWhere("SessionDeleteTable", "username"),
                    {contact.username}, options.outputDirectory));
    add(exportQuery(sessionDb, "session_draft",
                    selectAllWhere("SessionDraft", "username"),
                    {contact.username}, options.outputDirectory));
  }

  const auto resourcePath =
      options.databaseDirectory / "message" / "message_resource.db";
  if (std::filesystem::is_regular_file(resourcePath)) {
    ReadOnlyDatabase resourceDb(resourcePath, key);
    auto info = table("MessageResourceInfo", "i");
    auto chat = table("ChatName2Id", "c");
    auto sender = table("SenderName2Id", "s");
    auto infoJoin = WCDB::Join()
                        .table(info)
                        .join(chat)
                        .on(column("rowid", "c") == column("chat_id", "i"))
                        .leftJoin(sender)
                        .on(column("rowid", "s") == column("sender_id", "i"));
    const auto infoStatement =
        WCDB::StatementSelect()
            .select({WCDB::Column::all().table("i"),
                     column("user_name", "c").as("conversation_username"),
                     column("user_name", "s").as("sender_username")})
            .from(infoJoin)
            .where(column("user_name", "c") == WCDB::BindParameter())
            .orders({column("message_create_time", "i").asAscOrder(),
                     column("message_local_id", "i").asAscOrder()});
    add(exportQuery(resourceDb, "resource_info", infoStatement,
                    {contact.username}, options.outputDirectory));

    auto detail = table("MessageResourceDetail", "d");
    auto detailJoin =
        WCDB::Join()
            .table(detail)
            .join(info)
            .on(column("message_id", "i") == column("message_id", "d"))
            .join(chat)
            .on(column("rowid", "c") == column("chat_id", "i"));
    const auto detailStatement =
        WCDB::StatementSelect()
            .select({WCDB::Column::all().table("d"),
                     column("message_local_id", "i"),
                     column("message_svr_id", "i"),
                     column("user_name", "c").as("conversation_username")})
            .from(detailJoin)
            .where(column("user_name", "c") == WCDB::BindParameter())
            .orders({column("message_create_time", "i").asAscOrder(),
                     column("message_local_id", "i").asAscOrder(),
                     column("resource_id", "d").asAscOrder()});
    add(exportQuery(resourceDb, "resource_detail", detailStatement,
                    {contact.username}, options.outputDirectory));

    auto range = table("FtsRange", "r");
    auto rangeJoin = WCDB::Join().table(range).join(chat).on(
        column("rowid", "c") == column("session_id", "r"));
    const auto rangeStatement =
        WCDB::StatementSelect()
            .select({WCDB::Column::all().table("r"), column("user_name", "c")})
            .from(rangeJoin)
            .where(column("user_name", "c") == WCDB::BindParameter());
    add(exportQuery(resourceDb, "resource_fts_range", rangeStatement,
                    {contact.username}, options.outputDirectory));
  }

  const auto mediaPath = options.databaseDirectory / "message" / "media_0.db";
  if (std::filesystem::is_regular_file(mediaPath)) {
    ReadOnlyDatabase mediaDb(mediaPath, key);
    auto voice = table("VoiceInfo", "v");
    auto names = table("Name2Id", "n");
    auto join = WCDB::Join().table(voice).join(names).on(
        column("rowid", "n") == column("chat_name_id", "v"));
    const auto statement =
        WCDB::StatementSelect()
            .select({WCDB::Column::all().table("v"),
                     column("user_name", "n").as("conversation_username")})
            .from(join)
            .where(column("user_name", "n") == WCDB::BindParameter())
            .orders({column("create_time", "v").asAscOrder(),
                     column("local_id", "v").asAscOrder()});
    add(exportQuery(mediaDb, "voice", statement, {contact.username},
                    options.outputDirectory));
  }

  const auto ftsPath = options.databaseDirectory / "message" / "message_fts.db";
  if (std::filesystem::is_regular_file(ftsPath)) {
    ReadOnlyDatabase ftsDb(ftsPath, key);
    for (int partition = 0; partition < 4; ++partition) {
      const std::string suffix = std::to_string(partition);
      const std::string tableName = "message_fts_v4_" + suffix + "_content";
      const std::string exportName = "fts_message_v4_" + suffix;
      if (!ftsDb.tableExists(tableName)) {
        add(createEmptyExport(exportName, options.outputDirectory));
        continue;
      }
      auto fts = table(tableName.c_str(), "f");
      auto names = table("name2id", "n");
      auto join = WCDB::Join().table(fts).join(names).on(column("rowid", "n") ==
                                                         column("c4", "f"));
      const auto statement =
          WCDB::StatementSelect()
              .select({column("id", "f").as("fts_rowid"),
                       column("c0", "f").as("acontent"),
                       column("c1", "f").as("message_local_id"),
                       column("c2", "f").as("sort_seq"),
                       column("c3", "f").as("local_type"),
                       column("c4", "f").as("session_id"),
                       column("c5", "f").as("sender_id"),
                       column("c6", "f").as("create_time"),
                       column("username", "n").as("conversation_username")})
              .from(join)
              .where(column("username", "n") == WCDB::BindParameter())
              .orders({column("c2", "f").asAscOrder(),
                       column("c1", "f").asAscOrder()});
      add(exportQuery(ftsDb, exportName, statement, {contact.username},
                      options.outputDirectory));
    }
    if (ftsDb.tableExists("message_fts_v4_range")) {
      auto range = table("message_fts_v4_range", "r");
      auto names = table("name2id", "n");
      auto join = WCDB::Join().table(range).join(names).on(
          column("rowid", "n") == column("session_id", "r"));
      const auto statement =
          WCDB::StatementSelect()
              .select({WCDB::Column::all().table("r"),
                       column("username", "n").as("conversation_username")})
              .from(join)
              .where(column("username", "n") == WCDB::BindParameter());
      add(exportQuery(ftsDb, "fts_range", statement, {contact.username},
                      options.outputDirectory));
    } else {
      add(createEmptyExport("fts_range", options.outputDirectory));
    }
  }

  const auto generalPath = options.databaseDirectory / "general" / "general.db";
  if (std::filesystem::is_regular_file(generalPath)) {
    ReadOnlyDatabase generalDb(generalPath, key);
    const struct {
      const char *name;
      const char *tableName;
      const char *columnName;
    } queries[] = {
        {"revoked_messages", "revokemessage", "to_user_name"},
        {"revoked_batch_messages", "revokebatchmessage", "session_name"},
        {"transfers", "transferTable", "session_name"},
        {"red_envelopes", "redEnvelopeTable", "session_name"},
        {"group_payments", "groupPayTable", "session_name"},
        {"forward_recent", "ForwardRecent", "username"},
        {"search_recent", "SearchRecent", "username"},
        {"friend_messages", "FMessageTable", "user_name_"},
        {"wa_contact", "wacontact", "user_name"},
        {"voip", "ilink_voip", "wx_chatroom_"},
    };
    for (const auto &query : queries) {
      if (generalDb.tableExists(query.tableName)) {
        add(exportQuery(generalDb, query.name,
                        selectAllWhere(query.tableName, query.columnName),
                        {contact.username}, options.outputDirectory));
      } else {
        add(createEmptyExport(query.name, options.outputDirectory));
      }
    }
  }

  const auto hardlinkPath =
      options.databaseDirectory / "hardlink" / "hardlink.db";
  if (std::filesystem::is_regular_file(hardlinkPath)) {
    ReadOnlyDatabase hardlinkDb(hardlinkPath, key);
    for (const std::string type : {"image", "video", "file"}) {
      const std::string tableName = type + "_hardlink_info_v4";
      const std::string exportName = type + "_hardlinks";
      if (!hardlinkDb.tableExists(tableName)) {
        add(createEmptyExport(exportName, options.outputDirectory));
        continue;
      }
      auto hardlink = table(tableName.c_str(), "h");
      auto directory = table("dir2id", "d");
      auto join = WCDB::Join().table(hardlink).join(directory).on(
          column("rowid", "d") == column("dir2", "h"));
      const auto statement =
          WCDB::StatementSelect()
              .select({WCDB::Column::all().table("h"),
                       column("username", "d").as("conversation_username")})
              .from(join)
              .where(column("username", "d") == WCDB::BindParameter())
              .orders({column("modify_time", "h").asAscOrder(),
                       column("_rowid_", "h").asAscOrder()});
      add(exportQuery(hardlinkDb, exportName, statement, {contact.username},
                      options.outputDirectory));
    }
    if (hardlinkDb.tableExists("talker_checkpoint_v4")) {
      auto checkpoint = table("talker_checkpoint_v4", "c");
      auto directory = table("dir2id", "d");
      auto join = WCDB::Join()
                      .table(checkpoint)
                      .join(directory)
                      .on(column("rowid", "d") == column("talker_id", "c"));
      const auto statement =
          WCDB::StatementSelect()
              .select({WCDB::Column::all().table("c"),
                       column("username", "d").as("conversation_username")})
              .from(join)
              .where(column("username", "d") == WCDB::BindParameter());
      add(exportQuery(hardlinkDb, "hardlink_checkpoints", statement,
                      {contact.username}, options.outputDirectory));
    } else {
      add(createEmptyExport("hardlink_checkpoints", options.outputDirectory));
    }
  }

  const auto headImagePath =
      options.databaseDirectory / "head_image" / "head_image.db";
  if (std::filesystem::is_regular_file(headImagePath)) {
    ReadOnlyDatabase headImageDb(headImagePath, key);
    add(exportQuery(headImageDb, "head_image",
                    selectAllWhere("head_image", "username"),
                    {contact.username}, options.outputDirectory));
  }

  const auto favoritePath =
      options.databaseDirectory / "favorite" / "favorite.db";
  if (std::filesystem::is_regular_file(favoritePath)) {
    ReadOnlyDatabase favoriteDb(favoritePath, key);
    auto item = table("fav_db_item", "f");
    auto source = table("Name2Id", "src");
    auto chat = table("Name2Id", "chat");
    auto join =
        WCDB::Join()
            .table(item)
            .leftJoin(source)
            .on(column("rowid", "src") == column("fromusr_id", "f"))
            .leftJoin(chat)
            .on(column("rowid", "chat") == column("realchatname_id", "f"));
    const auto statement =
        WCDB::StatementSelect()
            .select({WCDB::Column::all().table("f"),
                     column("user_name", "src").as("from_username"),
                     column("user_name", "chat").as("real_chat_username")})
            .from(join)
            .where((column("user_name", "src") == WCDB::BindParameter(1)) ||
                   (column("user_name", "chat") == WCDB::BindParameter(2)))
            .orders({column("update_time", "f").asAscOrder(),
                     column("local_id", "f").asAscOrder()});
    add(exportQuery(favoriteDb, "favorites", statement,
                    {contact.username, contact.username},
                    options.outputDirectory));
  }

  const auto solitairePath =
      options.databaseDirectory / "solitaire" / "solitaire.db";
  if (std::filesystem::is_regular_file(solitairePath)) {
    ReadOnlyDatabase solitaireDb(solitairePath, key);
    for (const std::string prefix :
         {"Solitaire_", "SolitaireFold_", "SolitaireValid_"}) {
      const std::string tableName = prefix + digest;
      const std::string exportName = prefix.substr(0, prefix.size() - 1);
      if (solitaireDb.tableExists(tableName)) {
        const auto statement =
            WCDB::StatementSelect().select(WCDB::Column::all()).from(tableName);
        add(exportQuery(solitaireDb, exportName, statement, {},
                        options.outputDirectory));
      } else {
        add(createEmptyExport(exportName, options.outputDirectory));
      }
    }
  }

  writeManifest(options, contact, isGroup, messageTable, exports);
  const auto findRows = [&](const std::string_view name) {
    const auto found = std::find_if(
        exports.begin(), exports.end(),
        [&](const ExportResult &item) { return item.name == name; });
    return found == exports.end() ? int64_t{0} : found->rows;
  };
  const nlohmann::json summary{
      {"ok", true},
      {"conversation_kind", isGroup ? "group" : "user"},
      {"messages", findRows("messages")},
      {"biz_messages", findRows("biz_messages")},
      {"resource_info", findRows("resource_info")},
      {"voice", findRows("voice")},
      {"files", exports.size() + 1},
      {"output", pathUtf8(std::filesystem::absolute(options.outputDirectory))},
  };
  std::cout << summary.dump() << '\n';
  return 0;
}

} // namespace wechat::chat_exporter
