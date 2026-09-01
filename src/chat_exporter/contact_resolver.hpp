#pragma once

#include "model.hpp"

#include <vector>

namespace wechat::chat_exporter {

class ReadOnlyDatabase;

std::vector<ContactMatch>
listExportableContacts(ReadOnlyDatabase &contactDatabase,
                       ReadOnlyDatabase &messageDatabase);

} // namespace wechat::chat_exporter
