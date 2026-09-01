#include "contact_resolver.hpp"

#include "database.hpp"
#include "utility.hpp"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace wechat::chat_exporter {
namespace {

std::string displayName(const ContactMatch &contact) {
  if (!contact.nickname.empty()) {
    return contact.nickname;
  }
  if (!contact.alias.empty()) {
    return contact.alias;
  }
  return contact.username;
}

std::string sortName(const ContactMatch &contact) {
  return contact.remark.empty() ? displayName(contact) : contact.remark;
}

} // namespace

std::vector<ContactMatch>
listExportableContacts(ReadOnlyDatabase &contactDatabase,
                       ReadOnlyDatabase &messageDatabase) {
  std::unordered_set<std::string> messageTables;
  const auto tableStatement =
      WCDB::StatementSelect()
          .select(column("name"))
          .from(WCDB::TableOrSubquery::master())
          .where(column("type") == "table");
  messageDatabase.forEach(tableStatement, {}, [&](WCDB::Handle &handle) {
    std::string name = textAt(handle, 0);
    if (name.starts_with("Msg_")) {
      messageTables.emplace(std::move(name));
    }
  });

  std::unordered_map<std::string, ContactMatch> contactsByUsername;
  const auto contactStatement =
      WCDB::StatementSelect()
          .select({column("id"), column("username"), column("alias"),
                   column("remark"), column("nick_name")})
          .from("contact");
  contactDatabase.forEach(contactStatement, {}, [&](WCDB::Handle &handle) {
    ContactMatch contact;
    contact.id = handle.getInteger(0);
    contact.username = textAt(handle, 1);
    contact.alias = textAt(handle, 2);
    contact.remark = textAt(handle, 3);
    contact.nickname = textAt(handle, 4);
    if (!contact.username.empty()) {
      contactsByUsername.insert_or_assign(contact.username,
                                          std::move(contact));
    }
  });

  std::vector<ContactMatch> exportable;
  std::unordered_set<std::string> exportableUsernames;
  const auto sessionStatement =
      WCDB::StatementSelect()
          .select({WCDB::Column::rowid(), column("user_name")})
          .from("Name2Id");
  messageDatabase.forEach(sessionStatement, {}, [&](WCDB::Handle &handle) {
    const std::string username = textAt(handle, 1);
    if (username.empty() ||
        !messageTables.contains("Msg_" + md5Hex(username)) ||
        !exportableUsernames.emplace(username).second) {
      return;
    }
    const auto found = contactsByUsername.find(username);
    if (found != contactsByUsername.end()) {
      exportable.push_back(found->second);
    } else {
      ContactMatch contact;
      contact.id = handle.getInteger(0);
      contact.username = username;
      exportable.push_back(std::move(contact));
    }
  });

  std::sort(exportable.begin(), exportable.end(),
            [](const ContactMatch &left, const ContactMatch &right) {
              const std::string leftName = sortName(left);
              const std::string rightName = sortName(right);
              if (leftName != rightName) {
                return leftName < rightName;
              }
              return left.username < right.username;
            });
  return exportable;
}

} // namespace wechat::chat_exporter
