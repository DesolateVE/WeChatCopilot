#define NOMINMAX
#include <windows.h>
#include <detours/detours.h>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace
{
std::wstring QuoteCommandLineArgument(const std::wstring& argument)
{
    std::wstring quoted = L"\"";
    size_t backslashCount = 0;
    for (const wchar_t character : argument)
    {
        if (character == L'\\')
        {
            ++backslashCount;
            continue;
        }
        if (character == L'\"')
        {
            quoted.append(backslashCount * 2 + 1, L'\\');
        }
        else
        {
            quoted.append(backslashCount, L'\\');
        }
        quoted.push_back(character);
        backslashCount = 0;
    }
    quoted.append(backslashCount * 2, L'\\');
    quoted.push_back(L'\"');
    return quoted;
}

std::filesystem::path LauncherDirectory()
{
    std::vector<wchar_t> path(32768);
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length == path.size())
    {
        return {};
    }
    return std::filesystem::path(std::wstring(path.data(), length)).parent_path();
}

void PrintWindowsError(const wchar_t* operation, DWORD error)
{
    wchar_t* message = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
                           | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr,
                   error,
                   0,
                   reinterpret_cast<wchar_t*>(&message),
                   0,
                   nullptr);
    std::wcerr << operation << L" failed (" << error << L")";
    if (message != nullptr)
    {
        std::wcerr << L": " << message;
        LocalFree(message);
    }
    else
    {
        std::wcerr << L'\n';
    }
}
} // namespace

int wmain(int argc, wchar_t* argv[])
{
    if (argc < 2)
    {
        std::wcerr << L"Usage: chat_launcher.exe <path-to-Weixin.exe> [Weixin arguments...]\n";
        return 2;
    }

    std::error_code pathError;
    const std::filesystem::path weixinPath = std::filesystem::absolute(argv[1], pathError);
    if (pathError || !std::filesystem::is_regular_file(weixinPath, pathError))
    {
        std::wcerr << L"Weixin executable not found: " << argv[1] << L'\n';
        return 2;
    }

    std::wstring commandLine = QuoteCommandLineArgument(weixinPath.wstring());
    for (int index = 2; index < argc; ++index)
    {
        commandLine.push_back(L' ');
        commandLine += QuoteCommandLineArgument(argv[index]);
    }
    std::vector<wchar_t> mutableCommandLine(commandLine.begin(), commandLine.end());
    mutableCommandLine.push_back(L'\0');

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo{};
    const std::wstring workingDirectory = weixinPath.parent_path().wstring();

    const std::filesystem::path pluginPath = LauncherDirectory() / L"chat_plugin.dll";
    if (!std::filesystem::is_regular_file(pluginPath, pathError))
    {
        std::wcerr << L"Plugin not found beside launcher: " << pluginPath.wstring() << L'\n';
        return 2;
    }
    const std::string pluginPathString = pluginPath.string();
    LPCSTR dllPaths[] = { pluginPathString.c_str() };
    const BOOL created = DetourCreateProcessWithDllsW(weixinPath.c_str(),
                                                       mutableCommandLine.data(),
                                                       nullptr,
                                                       nullptr,
                                                       FALSE,
                                                       0,
                                                       nullptr,
                                                       workingDirectory.c_str(),
                                                       &startupInfo,
                                                       &processInfo,
                                                       1,
                                                       dllPaths,
                                                       nullptr);
    if (!created)
    {
        PrintWindowsError(L"DetourCreateProcessWithDllsW", GetLastError());
        return 1;
    }

    std::wcout << L"Started Weixin.exe with chat_plugin.dll, PID " << processInfo.dwProcessId << L'\n';
    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);

    return 0;
}