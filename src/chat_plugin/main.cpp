#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include <detours/detours.h>
#include <httplib.h>

#include "versions.hpp"

struct WCDB_Data
{
    uint8_t unknown_00[8];
    uint8_t* bytes; // +0x08：32 字节明文 key input
    uint64_t size;  // +0x10：长度，观察值为 32
};

using SetCipherKeyFn = void(__fastcall*)(void* database,           // RCX：WCDB Database/包装对象
                                         const WCDB_Data* keyData, // RDX：密钥 Data
                                         uint32_t pageSize,        // R8D：4096
                                         uint32_t cipherVersion);  // R9D：4

using LoadLibraryExWFn = HMODULE(WINAPI*)(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags);

SetCipherKeyFn RealSetCipherKey = nullptr;
LoadLibraryExWFn RealLoadLibraryExW = LoadLibraryExW;

std::atomic_bool g_weixinHookInstalled = false;
std::vector<uint8_t> g_weixinKeyBytes;
httplib::Server g_pluginServer;

extern "C" void DetourEntryPoint() {}

std::string GetModuleFileVersion(HMODULE module)
{
    std::vector<wchar_t> modulePath(32768);
    const DWORD pathLength =
            GetModuleFileNameW(module, modulePath.data(), static_cast<DWORD>(modulePath.size()));
    if (pathLength == 0 || pathLength == modulePath.size())
    {
        return {};
    }

    DWORD ignored = 0;
    const DWORD versionInfoSize = GetFileVersionInfoSizeW(modulePath.data(), &ignored);
    if (versionInfoSize == 0)
    {
        return {};
    }

    std::vector<uint8_t> versionInfo(versionInfoSize);
    if (!GetFileVersionInfoW(modulePath.data(), 0, versionInfoSize, versionInfo.data()))
    {
        return {};
    }

    VS_FIXEDFILEINFO* fixedInfo = nullptr;
    UINT fixedInfoSize = 0;
    if (!VerQueryValueW(versionInfo.data(), L"\\", reinterpret_cast<void**>(&fixedInfo), &fixedInfoSize)
        || fixedInfo == nullptr || fixedInfoSize < sizeof(VS_FIXEDFILEINFO)
        || fixedInfo->dwSignature != 0xFEEF04BD)
    {
        return {};
    }

    return std::to_string(HIWORD(fixedInfo->dwFileVersionMS)) + "."
           + std::to_string(LOWORD(fixedInfo->dwFileVersionMS)) + "."
           + std::to_string(HIWORD(fixedInfo->dwFileVersionLS)) + "."
           + std::to_string(LOWORD(fixedInfo->dwFileVersionLS));
}

bool MatchesSetCipherKeySignature(HMODULE module, uintptr_t rva)
{
    const auto base = reinterpret_cast<uintptr_t>(module);
    const auto* dosHeader = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE || dosHeader->e_lfanew <= 0)
    {
        return false;
    }

    const auto* ntHeaders = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dosHeader->e_lfanew);
    if (ntHeaders->Signature != IMAGE_NT_SIGNATURE || rva > ntHeaders->OptionalHeader.SizeOfImage
        || SetCipherKeyEntrySignature.size() > ntHeaders->OptionalHeader.SizeOfImage - rva)
    {
        return false;
    }

    const auto* code = reinterpret_cast<const uint8_t*>(base + rva);
    for (size_t index = 0; index < SetCipherKeyEntrySignature.size(); ++index)
    {
        const int16_t expected = SetCipherKeyEntrySignature[index];
        if (expected >= 0 && code[index] != static_cast<uint8_t>(expected))
        {
            return false;
        }
    }
    return true;
}

void __fastcall HookSetCipherKey(void* database, const WCDB_Data* keyData, int pageSize, int cipherVersion)
{
    RealSetCipherKey(database, keyData, pageSize, cipherVersion);

    if (g_weixinKeyBytes.empty())
    {
        g_weixinKeyBytes.resize(keyData->size);
        std::copy(keyData->bytes, keyData->bytes + keyData->size, g_weixinKeyBytes.begin());
        OutputDebugStringA("Weixin key bytes captured and stored.");
        // std::string keyHex;
        // for (const auto& byte : g_weixinKeyBytes)
        // {
        //     char buffer[3];
        //     sprintf_s(buffer, "%02X", byte);
        //     keyHex += buffer;
        // }
        // OutputDebugStringA(("Weixin key bytes (hex): " + keyHex).c_str());

        std::jthread(
                []()
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                    DetourTransactionBegin();
                    DetourUpdateThread(GetCurrentThread());
                    DetourDetach(reinterpret_cast<PVOID*>(&RealSetCipherKey), reinterpret_cast<PVOID>(HookSetCipherKey));
                    DetourTransactionCommit();
                })
                .detach();
    }
}

bool InstallWeixinHook(HMODULE module)
{
    const std::string version = GetModuleFileVersion(module);
    if (version.empty())
    {
        OutputDebugStringA("Unable to read the loaded Weixin.dll version; refusing to install the hook.");
        return false;
    }

    const WechatFunctionOffset* functions = FindWechatFunctionOffsets(version);
    if (functions == nullptr)
    {
        const std::string message = "Unsupported Weixin.dll version " + version
                                    + "; refusing to install the hook.";
        OutputDebugStringA(message.c_str());
        return false;
    }

    if (!MatchesSetCipherKeySignature(module, functions->setCipherKeyOffset))
    {
        const std::string message = "SetCipherKey signature mismatch for Weixin.dll " + version
                                    + "; refusing to install the hook.";
        OutputDebugStringA(message.c_str());
        return false;
    }

    bool expected = false;
    if (!g_weixinHookInstalled.compare_exchange_strong(expected, true))
    {
        return true;
    }

    RealSetCipherKey = reinterpret_cast<SetCipherKeyFn>(reinterpret_cast<uintptr_t>(module)
                                                        + functions->setCipherKeyOffset);

    LONG error = DetourTransactionBegin();
    if (error != NO_ERROR)
    {
        RealSetCipherKey = nullptr;
        g_weixinHookInstalled.store(false);
        OutputDebugStringA("Failed to begin the SetCipherKey hook transaction.");
        return false;
    }

    error = DetourUpdateThread(GetCurrentThread());
    if (error == NO_ERROR)
    {
        error = DetourAttach(reinterpret_cast<PVOID*>(&RealSetCipherKey),
                             reinterpret_cast<PVOID>(HookSetCipherKey));
    }
    if (error != NO_ERROR)
    {
        DetourTransactionAbort();
        RealSetCipherKey = nullptr;
        g_weixinHookInstalled.store(false);
        OutputDebugStringA("Failed to prepare the SetCipherKey hook transaction.");
        return false;
    }

    error = DetourTransactionCommit();
    if (error != NO_ERROR)
    {
        RealSetCipherKey = nullptr;
        g_weixinHookInstalled.store(false);
        OutputDebugStringA("Failed to commit the SetCipherKey hook transaction.");
        return false;
    }

    const std::string message = "SetCipherKey hook installed for Weixin.dll " + version + ".";
    OutputDebugStringA(message.c_str());
    return true;
}

HMODULE WINAPI HookLoadLibraryExW(LPCWSTR fileName, HANDLE file, DWORD flags)
{
    // 返回时目标映像、导入表和 DllMain 已经处理完成。
    HMODULE result = RealLoadLibraryExW(fileName, file, flags);

    // 即使 Weixin.dll 是作为其他 DLL 的依赖加载，也能在这里检查出来。
    if (!g_weixinHookInstalled.load())
    {
        if (HMODULE weixin = GetModuleHandleW(L"Weixin.dll"))
        {
            InstallWeixinHook(weixin);
        }
    }

    // 在返回给原调用者之前，目标 Hook 已安装。
    return result;
}

void InitPluginServer()
{
    g_pluginServer.Get("/key/string",
                       [](const httplib::Request& req, httplib::Response& res)
                       {
                           if (g_weixinKeyBytes.empty())
                           {
                               res.status = 404;
                               res.set_content("Weixin key not captured yet.", "text/plain");
                               return;
                           }
                           std::string keyHex;
                           for (const auto& byte : g_weixinKeyBytes)
                           {
                               char buffer[3];
                               sprintf_s(buffer, "%02X", byte);
                               keyHex += buffer;
                           }
                           res.set_content(keyHex, "text/plain");
                       });

    g_pluginServer.Get("/key/bytes",
                       [](const httplib::Request& req, httplib::Response& res)
                       {
                           if (g_weixinKeyBytes.empty())
                           {
                               res.status = 404;
                               res.set_content("Weixin key not captured yet.", "text/plain");
                               return;
                           }
                           res.set_content(reinterpret_cast<const char*>(g_weixinKeyBytes.data()), g_weixinKeyBytes.size(),
                                           "application/octet-stream");
                       });

    std::jthread(
            []()
            {
                OutputDebugStringA("Starting plugin server on port 6500...");
                g_pluginServer.listen("127.0.0.1", 6500);
            })
            .detach();
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (DetourIsHelperProcess())
    {
        return TRUE;
    }

    if (reason == DLL_PROCESS_ATTACH)
    {
        OutputDebugStringA("chat_plugin.dll loaded, installing hooks...");

        InitPluginServer();

        DisableThreadLibraryCalls(instance);
        DetourRestoreAfterWith();

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourAttach(reinterpret_cast<PVOID*>(&RealLoadLibraryExW), reinterpret_cast<PVOID>(HookLoadLibraryExW));
        DetourTransactionCommit();
    }

    return TRUE;
}
