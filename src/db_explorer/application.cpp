#include "application.hpp"

#include "database_access.hpp"
#include "http_service.hpp"
#include "tools.hpp"

#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace wechat {

Application::Application(ApplicationConfig config)
    : config_(std::move(config))
{
}

int Application::run(int argc, char* argv[]) const
{
    auto keyBytes = wx_plugin::HexStringToBytes(config_.keyHex);
    DatabaseAccess databaseAccess(config_.databaseDirectory, std::move(keyBytes));
    const auto databases = databaseAccess.listDatabases();
    if (databases.empty()) {
        std::cerr << "No database files found under " << config_.databaseDirectory.string() << std::endl;
        return 1;
    }

    if (argc == 3 && std::string_view(argv[1]) == "--schema-json") {
        std::ofstream output(argv[2], std::ios::binary);
        if (!output) {
            throw std::runtime_error("Failed to create schema JSON file");
        }
        output << databaseAccess.schemaCatalog().dump(
                      2, ' ', false, nlohmann::json::error_handler_t::replace)
               << '\n';
        std::cout << "Schema JSON written to " << argv[2] << std::endl;
        return 0;
    }

    std::cout << "Found " << databases.size() << " database files under "
              << config_.databaseDirectory.string() << std::endl;
    HttpService httpService(databaseAccess, config_.port, config_.defaultPageSize);
    return httpService.run() ? 0 : 1;
}

} // namespace wechat