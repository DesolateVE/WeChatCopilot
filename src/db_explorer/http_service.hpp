#pragma once

#include <cstddef>

namespace wechat {

class DatabaseAccess;

class HttpService {
public:
    HttpService(const DatabaseAccess& databaseAccess, int port, size_t defaultPageSize);

    bool run() const;

private:
    const DatabaseAccess& databaseAccess_;
    int port_;
    size_t defaultPageSize_;
};

} // namespace wechat