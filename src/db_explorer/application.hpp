#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

namespace wechat {

struct ApplicationConfig {
    std::string keyHex;
    std::filesystem::path databaseDirectory;
    int port = 8090;
    size_t defaultPageSize = 20;
};

class Application {
public:
    explicit Application(ApplicationConfig config);

    int run(int argc, char* argv[]) const;

private:
    ApplicationConfig config_;
};

} // namespace wechat