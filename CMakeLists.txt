cmake_minimum_required(VERSION 3.24)
project(GeMusic VERSION 0.1.0 LANGUAGES CXX)

# C++20 标准，禁用编译器扩展
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 导出 compile_commands.json 供 clang-tidy 使用
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ============================================================
# 第三方依赖（FetchContent）
# ============================================================
include(FetchContent)

# FTXUI - 终端 UI 框架（使用本地已下载的源码）
FetchContent_Declare(
    ftxui
    SOURCE_DIR ${CMAKE_BINARY_DIR}/_deps/ftxui-src
)

# nlohmann/json - JSON 解析（使用本地已下载的源码）
FetchContent_Declare(
    json
    SOURCE_DIR ${CMAKE_BINARY_DIR}/_deps/json-src
)

# yaml-cpp - YAML 配置文件解析（使用本地已下载的源码）
FetchContent_Declare(
    yaml-cpp
    SOURCE_DIR ${CMAKE_BINARY_DIR}/_deps/yaml-cpp-src
)

# GoogleTest - 单元测试（使用本地已下载的源码）
FetchContent_Declare(
    googletest
    SOURCE_DIR ${CMAKE_BINARY_DIR}/_deps/googletest-src
)

# spdlog - 日志库（使用本地已下载的源码）
FetchContent_Declare(
    spdlog
    SOURCE_DIR ${CMAKE_BINARY_DIR}/_deps/spdlog-src
)

# 统一拉取所有依赖
FetchContent_MakeAvailable(ftxui json yaml-cpp googletest spdlog)

# OpenSSL - AES-CBC + RSA 加密（网易云接口加密必须）
find_package(OpenSSL REQUIRED)

# qrcodegen - 二维码生成（nayuki/QR-Code-generator，仅两个文件）
# 使用本地已下载的源码，以 STATIC 库形式编译
add_library(qrcodegen STATIC
    ${CMAKE_BINARY_DIR}/_deps/qrcodegen-src/qrcodegen.cpp
)
target_include_directories(qrcodegen PUBLIC
    ${CMAKE_BINARY_DIR}/_deps/qrcodegen-src
)
# qrcodegen 内部不符合严格警告，单独关闭 Werror
target_compile_options(qrcodegen PRIVATE -w)

# ============================================================
# 主程序目标
# ============================================================

# 收集源文件
file(GLOB_RECURSE GEMUSIC_SOURCES src/*.cpp)

add_executable(GeMusic ${GEMUSIC_SOURCES})

target_include_directories(GeMusic PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(GeMusic PRIVATE
    ftxui::screen
    ftxui::dom
    ftxui::component
    nlohmann_json::nlohmann_json
    yaml-cpp::yaml-cpp
    OpenSSL::Crypto
    qrcodegen
    spdlog::spdlog
    curl
    pthread
)

# 编译警告（严格模式）
target_compile_options(GeMusic PRIVATE
    -Wall -Wextra -Wpedantic -Werror
)

# ============================================================
# 测试目标
# ============================================================
enable_testing()

file(GLOB TEST_SOURCES tests/*.cpp)

if(TEST_SOURCES)
    # 收集需要链接到测试的项目源文件（排除 main.cpp）
    file(GLOB_RECURSE GEMUSIC_LIB_SOURCES src/*.cpp)
    list(FILTER GEMUSIC_LIB_SOURCES EXCLUDE REGEX "src/main\\.cpp$")

    add_executable(tests ${TEST_SOURCES} ${GEMUSIC_LIB_SOURCES})

    target_include_directories(tests PRIVATE
        ${CMAKE_SOURCE_DIR}/include
    )

    target_link_libraries(tests PRIVATE
        GTest::gtest_main
        nlohmann_json::nlohmann_json
        yaml-cpp::yaml-cpp
        ftxui::screen
        ftxui::dom
        ftxui::component
        OpenSSL::Crypto
        qrcodegen
        spdlog::spdlog
        curl
        pthread
    )

    include(GoogleTest)
    gtest_discover_tests(tests)
endif()
