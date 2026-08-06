cmake_minimum_required(VERSION 3.24)

project(WeixinWCDBTools LANGUAGES C CXX)

find_package(nlohmann_json CONFIG REQUIRED)

if(NOT MSVC)
    message(FATAL_ERROR "The checked-in build recipe currently targets MSVC/Windows.")
endif()

set(WCDB_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.deps/wcdb" CACHE PATH
    "Path to a WCDB source checkout")
set(WCDB_BUILD_ROOT "${WCDB_ROOT}/build-static-mt-x64" CACHE PATH
    "Path to the configured static WCDB build")

set(WCDB_INCLUDE_DIR "${WCDB_BUILD_ROOT}/export_headers")
set(WCDB_RELEASE_DIR "${WCDB_BUILD_ROOT}/Release")
set(WCDB_OPENSSL_DIR
    "${WCDB_ROOT}/tools/prebuild/openssl/windows/win64")
set(WCDB_ZLIB_DIR
    "${WCDB_ROOT}/tools/prebuild/zlib/windows/win64")

set(WCDB_REQUIRED_FILES
    "${WCDB_INCLUDE_DIR}/WCDB/WCDBCpp.h"
    "${WCDB_RELEASE_DIR}/WCDB.lib"
    "${WCDB_RELEASE_DIR}/sqlcipher.lib"
    "${WCDB_RELEASE_DIR}/zstd.lib"
    "${WCDB_OPENSSL_DIR}/libcrypto.lib"
    "${WCDB_ZLIB_DIR}/zlibstatic.lib")

foreach(required_file IN LISTS WCDB_REQUIRED_FILES)
    if(NOT EXISTS "${required_file}")
        message(FATAL_ERROR
            "Missing WCDB build input: ${required_file}\n"
            "Run scripts/build_wcdb_static.ps1 first.")
    endif()
endforeach()

add_executable(weixin_wcdb_reader
    src/native/weixin_wcdb_reader.cpp)

add_executable(weixin_chat_exporter
    src/native/weixin_chat_exporter.cpp)

target_compile_features(weixin_wcdb_reader PRIVATE cxx_std_17)
target_compile_options(weixin_wcdb_reader PRIVATE
    /utf-8 /W4 /permissive- /wd4068)
set_property(TARGET weixin_wcdb_reader PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

target_include_directories(weixin_wcdb_reader PRIVATE
    "${WCDB_INCLUDE_DIR}")

target_link_libraries(weixin_wcdb_reader PRIVATE
    "${WCDB_RELEASE_DIR}/WCDB.lib"
    "${WCDB_RELEASE_DIR}/sqlcipher.lib"
    "${WCDB_RELEASE_DIR}/zstd.lib"
    "${WCDB_OPENSSL_DIR}/libcrypto.lib"
    "${WCDB_ZLIB_DIR}/zlibstatic.lib"
    Crypt32
    Ws2_32)

target_compile_features(weixin_chat_exporter PRIVATE cxx_std_17)
target_compile_options(weixin_chat_exporter PRIVATE
    /utf-8 /W4 /permissive- /wd4068)
target_compile_definitions(weixin_chat_exporter PRIVATE SQLITE_HAS_CODEC)
set_property(TARGET weixin_chat_exporter PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

target_include_directories(weixin_chat_exporter PRIVATE
    "${WCDB_BUILD_ROOT}/include/sqlcipher"
    "${WCDB_BUILD_ROOT}/include/zstd")

target_link_libraries(weixin_chat_exporter PRIVATE
    nlohmann_json::nlohmann_json
    "${WCDB_RELEASE_DIR}/sqlcipher.lib"
    "${WCDB_RELEASE_DIR}/zstd.lib"
    "${WCDB_OPENSSL_DIR}/libcrypto.lib"
    "${WCDB_ZLIB_DIR}/zlibstatic.lib"
    Bcrypt
    Crypt32
    Ws2_32)
