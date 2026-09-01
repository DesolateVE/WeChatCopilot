#define NOMINMAX
#include <windows.h>

#include "application.hpp"
#include "options.hpp"

#include <exception>
#include <iostream>

int wmain(const int argc, wchar_t *argv[]) {
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCP(CP_UTF8);
  SetConsoleTitleW(L"WeChat 聊天记录导出");
  try {
    return wechat::chat_exporter::run(
        wechat::chat_exporter::parseOptions(argc, argv));
  } catch (const std::exception &error) {
    std::cerr << "error: " << error.what() << '\n';
    return 1;
  }
}
