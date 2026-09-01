#pragma once

#include "model.hpp"

#include <optional>
#include <vector>

namespace wechat::chat_exporter {

std::optional<ContactMatch>
selectExportContact(const std::vector<ContactMatch> &contacts);

} // namespace wechat::chat_exporter
