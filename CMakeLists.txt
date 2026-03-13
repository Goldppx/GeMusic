cmake_minimum_required(VERSION 3.24)
project(GeMusic VERSION 0.1.0 LANGUAGES CXX)

# C++20 标准，禁用编译器扩展
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 导出 compile_commands.json 供 clang-tidy 使用
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ============================================================
# 第三方依赖（FetchContent 在线下载）
# ============================================================
include(FetchContent)

# FTXUI 6.1.9 - 终端 UI 框架
FetchContent_Declare(
    ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
    GIT_TAG        v6.1.9
    GIT_SHALLOW    TRUE
)

# nlohmann/json 3.12.0 - JSON 解析
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.12.0
    GIT_SHALLOW    TRUE
)

# yaml-cpp 0.9.0 - YAML 配置文件解析
FetchContent_Declare(
    yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG        yaml-cpp-0.9.0
    GIT_SHALLOW    TRUE
)

# GoogleTest 1.16.0 - 单元测试
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.16.0
    GIT_SHALLOW    TRUE
)

# spdlog 1.15.3 - 日志库
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.15.3
    GIT_SHALLOW    TRUE
)

# 统一拉取所有基于 CMake 的依赖
FetchContent_MakeAvailable(ftxui json yaml-cpp googletest spdlog)

# OpenSSL - AES-CBC + RSA 加密（网易云接口加密必须）
find_package(OpenSSL REQUIRED)

# qrcodegen - 二维码生成（nayuki/QR-Code-generator v1.8.0）
# 该库只有两个文件，直接用 file(DOWNLOAD) 下载到 build 目录，避免触碰已弃用的
# FetchContent_Populate。文件已存在时跳过下载，不产生重复网络请求。
set(QRCODEGEN_VERSION "v1.8.0")
set(QRCODEGEN_BASE_URL
    "https://raw.githubusercontent.com/nayuki/QR-Code-generator/${QRCODEGEN_VERSION}/cpp"
)
set(QRCODEGEN_DIR "${CMAKE_BINARY_DIR}/_deps/qrcodegen")
if(NOT EXISTS "${QRCODEGEN_DIR}/qrcodegen.hpp")
    file(DOWNLOAD "${QRCODEGEN_BASE_URL}/qrcodegen.hpp" "${QRCODEGEN_DIR}/qrcodegen.hpp"
         STATUS _status)
    list(GET _status 0 _code)
    if(NOT _code EQUAL 0)
        message(FATAL_ERROR "下载 qrcodegen.hpp 失败：${_status}")
    endif()
endif()
if(NOT EXISTS "${QRCODEGEN_DIR}/qrcodegen.cpp")
    file(DOWNLOAD "${QRCODEGEN_BASE_URL}/qrcodegen.cpp" "${QRCODEGEN_DIR}/qrcodegen.cpp"
         STATUS _status)
    list(GET _status 0 _code)
    if(NOT _code EQUAL 0)
        message(FATAL_ERROR "下载 qrcodegen.cpp 失败：${_status}")
    endif()
endif()
add_library(qrcodegen STATIC "${QRCODEGEN_DIR}/qrcodegen.cpp")
target_include_directories(qrcodegen PUBLIC "${QRCODEGEN_DIR}")
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
