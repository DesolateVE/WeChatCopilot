#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace wechat::chat_exporter
{

class MessageEnricher final
{
  public:
    MessageEnricher(const std::filesystem::path& databaseDirectory, const std::vector<unsigned char>& key, const std::string& username);

    void augment(nlohmann::json& message) const;

  private:
    using JsonItem = std::shared_ptr<const nlohmann::json>;
    using ItemsByKey = std::unordered_map<std::string, std::vector<JsonItem>>;

    ItemsByKey resources_;
    ItemsByKey voices_;
    ItemsByKey hardlinksByMd5_;
};

} // namespace wechat::chat_exporter
