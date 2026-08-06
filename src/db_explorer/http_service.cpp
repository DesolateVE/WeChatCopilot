// clang-format off
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include "http_service.hpp"
#include "database_access.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>
// clang-format on

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace wechat {
namespace {

using Json = nlohmann::json;

std::string readHtmlPage()
{
    wchar_t executablePath[MAX_PATH];
    const DWORD pathLength = GetModuleFileNameW(nullptr, executablePath, MAX_PATH);
    if (pathLength == 0 || pathLength == MAX_PATH) {
        throw std::runtime_error("Failed to determine executable path");
    }

    const auto htmlPath = std::filesystem::path(std::wstring_view(executablePath, pathLength)).parent_path()
        / "web" / "index.html";
    std::ifstream input(htmlPath, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Failed to read HTML file: " + htmlPath.string());
    }

    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

void setNotFound(httplib::Response& response, const std::string& message)
{
    response.status = httplib::StatusCode::NotFound_404;
    response.set_content(message, "text/plain; charset=utf-8");
}

void setJson(httplib::Response& response, const Json& payload)
{
    response.set_content(payload.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace),
        "application/json; charset=utf-8");
}

void setBadRequest(httplib::Response& response, const std::string& message)
{
    response.status = httplib::StatusCode::BadRequest_400;
    setJson(response, Json { { "error", message } });
}

std::optional<size_t> numericParameter(const httplib::Request& request, const std::string& name)
{
    if (!request.has_param(name)) {
        return std::nullopt;
    }
    try {
        return static_cast<size_t>(std::stoull(request.get_param_value(name)));
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

bool setDatabaseError(httplib::Response& response, DatabaseStatus status)
{
    if (status == DatabaseStatus::NotFound) {
        setNotFound(response, "Database not found");
        return true;
    }
    if (status == DatabaseStatus::CannotOpen) {
        setNotFound(response, "Database could not be opened");
        return true;
    }
    return false;
}

} // namespace

HttpService::HttpService(
    const DatabaseAccess& databaseAccess, int port, size_t defaultPageSize)
    : databaseAccess_(databaseAccess)
    , port_(port)
    , defaultPageSize_(defaultPageSize)
{
}

bool HttpService::run() const
{
    httplib::Server server;

    server.set_exception_handler([](const httplib::Request&, httplib::Response& response, std::exception_ptr error) {
        std::string message = "Unknown exception";
        try {
            if (error) {
                std::rethrow_exception(error);
            }
        } catch (const std::exception& exception) {
            message = exception.what();
        }
        response.status = httplib::StatusCode::InternalServerError_500;
        response.set_content(Json { { "error", message } }.dump(), "application/json; charset=utf-8");
    });

    server.Get("/", [](const httplib::Request&, httplib::Response& response) {
        response.set_content(readHtmlPage(), "text/html; charset=utf-8");
    });

    server.Get("/api/databases", [this](const httplib::Request&, httplib::Response& response) {
        setJson(response, Json { { "databases", databaseAccess_.listDatabases() } });
    });

    server.Get("/api/tables", [this](const httplib::Request& request, httplib::Response& response) {
        if (!request.has_param("database")) {
            setBadRequest(response, "Missing required query parameter: database");
            return;
        }

        auto result = databaseAccess_.listTables(request.get_param_value("database"));
        if (!setDatabaseError(response, result.status)) {
            setJson(response, Json { { "tables", result.value } });
        }
    });

    server.Get("/api/schema", [this](const httplib::Request& request, httplib::Response& response) {
        if (!request.has_param("database")) {
            setBadRequest(response, "Missing required query parameter: database");
            return;
        }

        auto result = databaseAccess_.databaseSchema(request.get_param_value("database"));
        if (!setDatabaseError(response, result.status)) {
            setJson(response, result.value);
        }
    });

    server.Get("/api/schema/:table", [this](const httplib::Request& request, httplib::Response& response) {
        if (!request.has_param("database")) {
            setBadRequest(response, "Missing required query parameter: database");
            return;
        }

        const std::string databaseName = request.get_param_value("database");
        auto result = databaseAccess_.tableSchema(databaseName, request.path_params.at("table"));
        if (setDatabaseError(response, result.status)) {
            return;
        }
        if (result.status == DatabaseStatus::TableNotFound) {
            setNotFound(response, "Table not found");
            return;
        }
        setJson(response, Json { { "database", databaseName }, { "table", result.value } });
    });

    server.Get("/api/tables/:table", [this](const httplib::Request& request, httplib::Response& response) {
        if (!request.has_param("database")) {
            setBadRequest(response, "Missing required query parameter: database");
            return;
        }

        const size_t page = std::max<size_t>(1, numericParameter(request, "page").value_or(1));
        const size_t pageSize = std::clamp<size_t>(
            numericParameter(request, "pageSize").value_or(defaultPageSize_), 1, 100);
        const std::string tableName = request.path_params.at("table");
        auto result = databaseAccess_.queryTablePage(
            request.get_param_value("database"), tableName, page, pageSize);
        if (setDatabaseError(response, result.status)) {
            return;
        }

        const auto& tablePage = result.value;
        setJson(response,
            Json { { "table", tableName },
                { "columns", tablePage.columns },
                { "columnTypes", tablePage.columnTypes },
                { "columnPrimaryKeys", tablePage.columnPrimaryKeys },
                { "rows", tablePage.rows },
                { "zstdDecompressed", tablePage.zstdDecompressed },
                { "blobHexEncoded", tablePage.blobHexEncoded },
                { "totalRows", tablePage.totalRows },
                { "page", tablePage.page },
                { "pageSize", tablePage.pageSize },
                { "error", tablePage.error } });
    });

    std::cout << "HTTP server listening on http://127.0.0.1:" << port_ << std::endl;
    return server.listen("127.0.0.1", port_);
}

} // namespace wechat