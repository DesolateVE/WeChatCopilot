#pragma once
#include <optional>
#include <string>

#pragma comment(lib, "Version.lib")

struct FileVersions
{
    std::wstring file;
    std::wstring product;
};

std::optional<FileVersions> GetFileVersions(const std::wstring& path);