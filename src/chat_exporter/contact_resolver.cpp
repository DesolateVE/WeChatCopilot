#include "contact_resolver.hpp"

#include "database.hpp"

#include <algorithm>
#include <exception>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace wechat::chat_exporter {

std::vector<ContactMatch> findContacts(ReadOnlyDatabase &database,
                                       const std::string &query) {
  const auto statement =
      WCDB::StatementSelect()
          .select({column("id"), column("username"), column("alias"),
                   column("remark"), column("nick_name")})
          .from("contact")
          .where((column("username") == WCDB::BindParameter(1)) ||
                 (column("alias") == WCDB::BindParameter(2)) ||
                 (column("remark") == WCDB::BindParameter(3)) ||
                 (column("nick_name") == WCDB::BindParameter(4)));
  std::vector<ContactMatch> matches;
  database.forEach(statement, {query, query, query, query},
                   [&](WCDB::Handle &handle) {
                     ContactMatch match;
                     match.id = handle.getInteger(0);
                     match.username = textAt(handle, 1);
                     match.alias = textAt(handle, 2);
                     match.remark = textAt(handle, 3);
                     match.nickname = textAt(handle, 4);
                     if (match.username == query) {
                       match.priority = 0;
                     } else if (match.alias == query) {
                       match.priority = 1;
                     } else if (match.remark == query) {
                       match.priority = 2;
                     } else {
                       match.priority = 3;
                     }
                     matches.emplace_back(std::move(match));
                   });
  std::sort(matches.begin(), matches.end(),
            [](const auto &left, const auto &right) {
              if (left.priority != right.priority) {
                return left.priority < right.priority;
              }
              return left.username < right.username;
            });
  return matches;
}

ContactMatch chooseContact(const std::vector<ContactMatch> &allMatches,
                           const std::string &,
                           const std::string &selectedUsername) {
  if (allMatches.empty()) {
    throw std::runtime_error("no contact matches the query");
  }
  const int bestPriority = allMatches.front().priority;
  std::vector<ContactMatch> matches;
  std::copy_if(allMatches.begin(), allMatches.end(),
               std::back_inserter(matches),
               [bestPriority](const ContactMatch &match) {
                 return match.priority == bestPriority;
               });

  if (bestPriority <= 1) {
    if (matches.size() != 1) {
      throw std::runtime_error(
          "database violates username/alias uniqueness for the supplied query");
    }
    return matches.front();
  }
  if (matches.size() == 1) {
    return matches.front();
  }
  if (!selectedUsername.empty()) {
    const auto found = std::find_if(matches.begin(), matches.end(),
                                    [&](const ContactMatch &match) {
                                      return match.username == selectedUsername;
                                    });
    if (found == matches.end()) {
      throw std::runtime_error(
          "--select-username is not one of the matching contacts");
    }
    return *found;
  }

  std::cout << "查询值匹配到 " << matches.size() << " 个联系人，请选择：\n";
  for (size_t index = 0; index < matches.size(); ++index) {
    const auto &match = matches[index];
    std::cout << "  [" << index + 1 << "] username=" << match.username
              << " | alias=" << match.alias << " | remark=" << match.remark
              << " | nick_name=" << match.nickname << '\n';
  }
  while (true) {
    std::cout << "输入序号（1-" << matches.size() << "）：" << std::flush;
    std::string input;
    if (!std::getline(std::cin, input)) {
      throw std::runtime_error("candidate selection requires interactive "
                               "input; use --select-username");
    }
    try {
      size_t consumed = 0;
      const unsigned long selection = std::stoul(input, &consumed);
      if (consumed == input.size() && selection >= 1 &&
          selection <= matches.size()) {
        return matches[selection - 1];
      }
    } catch (const std::exception &) {
    }
    std::cout << "无效选择，请重试。\n";
  }
}

} // namespace wechat::chat_exporter
