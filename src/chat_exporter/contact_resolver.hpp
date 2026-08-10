#pragma once

#include "model.hpp"

#include <string>
#include <vector>

namespace wechat::chat_exporter {

class ReadOnlyDatabase;

std::vector<ContactMatch> findContacts(ReadOnlyDatabase &database,
                                       const std::string &query);

ContactMatch chooseContact(const std::vector<ContactMatch> &matches,
                           const std::string &query,
                           const std::string &selectedUsername);

} // namespace wechat::chat_exporter
