#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <atomic>
#include <vector>
#include <string>
#include <thread>

#include <detours/detours.h>
#include <httplib.h>

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
    bool expected = false;
    if (!g_weixinHookInstalled.compare_exchange_strong(expected, true))
    {
        return true;
    }

    constexpr uintptr_t kSetCipherKeyRva = 0x5DBF40;
    RealSetCipherKey = reinterpret_cast<SetCipherKeyFn>(reinterpret_cast<uintptr_t>(module) + kSetCipherKeyRva);

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(reinterpret_cast<PVOID*>(&RealSetCipherKey), reinterpret_cast<PVOID>(HookSetCipherKey));
    DetourTransactionCommit();

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