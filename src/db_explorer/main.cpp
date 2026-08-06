#include "application.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

std::string requiredEnvironment(const char* name)
{
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        throw std::runtime_error(std::string("Missing required environment variable: ") + name);
    }
    return value;
}

std::string optionalEnvironment(const char* name, const char* fallback)
{
    const char* value = std::getenv(name);
    return value == nullptr || *value == '\0' ? fallback : value;
}

} // namespace

int main(int argc, char* argv[])
{
    try {
        const wechat::ApplicationConfig config {
            .keyHex = requiredEnvironment("WECHAT_DB_KEY_HEX"),
            .databaseDirectory = optionalEnvironment("WECHAT_DB_DIR", "local-data/db-storage"),
            .port = 8090,
            .defaultPageSize = 20,
        };
        return wechat::Application(config).run(argc, argv);
    } catch (const std::exception& exception) {
        std::cerr << "Error: " << exception.what() << std::endl;
        return 1;
    }
}
