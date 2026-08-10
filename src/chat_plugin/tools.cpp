#include "tools.hpp"
#include <windows.h>
#include <vector>

static std::wstring FormatVersion(DWORD ms, DWORD ls)
{
    return std::to_wstring(HIWORD(ms)) + L"." + std::to_wstring(LOWORD(ms)) + L"." + std::to_wstring(HIWORD(ls)) + L"." +
           std::to_wstring(LOWORD(ls));
}

std::optional<FileVersions> GetFileVersions(const std::wstring& path)
{
    DWORD handle = 0;
    const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &handle);
    if (size == 0)
        return std::nullopt;

    std::vector<BYTE> buffer(size);
    if (!GetFileVersionInfoW(path.c_str(), 0, size, buffer.data()))
    {
        return std::nullopt;
    }

    VS_FIXEDFILEINFO* info = nullptr;
    UINT infoSize = 0;

    if (!VerQueryValueW(buffer.data(), L"\\", reinterpret_cast<void**>(&info), &infoSize) || info == nullptr ||
        infoSize < sizeof(VS_FIXEDFILEINFO) || info->dwSignature != 0xFEEF04BD)
    {
        return std::nullopt;
    }

    return FileVersions{
            FormatVersion(info->dwFileVersionMS, info->dwFileVersionLS),
            FormatVersion(info->dwProductVersionMS, info->dwProductVersionLS),
    };
}