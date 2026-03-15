cmake_minimum_required(VERSION 3.24)
project(GeMusic VERSION 0.1.0 LANGUAGES CXX)

# C++20 标准，禁用编译器扩展
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 导出 compile_commands.json 供 clang-tidy 使用
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ============================================================
# 构建模式选项
# ============================================================
# USE_SYSTEM_LIBS=ON  → 优先使用系统已安装的包（本地构建，速度快）
# USE_SYSTEM_LIBS=OFF → 全部通过 FetchContent 在线下载（默认行为，兼容性好）
#
# 用法示例：
#   cmake -B build -DUSE_SYSTEM_LIBS=ON       # 本地模式
#   cmake -B build                              # 在线模式（默认）
option(USE_SYSTEM_LIBS "优先使用系统已安装的库，而非 FetchContent 在线下载" ON)

include(FetchContent)

# ============================================================
# 辅助宏：尝试 find_package，失败时回退到 FetchContent
# ============================================================
# 当 USE_SYSTEM_LIBS=ON 时先尝试 find_package，找不到则自动回退 FetchContent。
# 当 USE_SYSTEM_LIBS=OFF 时直接走 FetchContent。
macro(try_system_or_fetch _name)
    # 解析 FETCH 关键字，将其前后参数分别作为 find_package / FetchContent_Declare 参数
    set(_fetch_args)
    set(_find_args)
    set(_in_fetch FALSE)
    foreach(_arg ${ARGN})
        if("${_arg}" STREQUAL "FETCH")
            set(_in_fetch TRUE)
        elseif(_in_fetch)
            list(APPEND _fetch_args "${_arg}")
        else()
            list(APPEND _find_args "${_arg}")
        endif()
    endforeach()

    set(_found_system_${_name} FALSE)
    if(USE_SYSTEM_LIBS)
        find_package(${_find_args} QUIET)
        # find_package 设置 <PackageName>_FOUND，包名大小写可能不同
        string(TOUPPER "${_name}" _NAME_UPPER)
        if(${_name}_FOUND OR ${_NAME_UPPER}_FOUND)
            set(_found_system_${_name} TRUE)
            message(STATUS "[GeMusic] 使用系统包: ${_name}")
        endif()
    endif()

    if(NOT _found_system_${_name})
        if(USE_SYSTEM_LIBS)
            message(STATUS "[GeMusic] 系统未找到 ${_name}，回退到 FetchContent")
        else()
            message(STATUS "[GeMusic] FetchContent 在线下载: ${_name}")
        endif()
        FetchContent_Declare(${_name} ${_fetch_args})
        # 记录需要 MakeAvailable 的目标
        list(APPEND _GEMUSIC_FETCH_TARGETS ${_name})
    endif()
endmacro()

# ============================================================
# 第三方依赖声明
# ============================================================
set(_GEMUSIC_FETCH_TARGETS "")

# --- FTXUI 6.1.9 - 终端 UI 框架 ---
# 注意：FTXUI 没有主流发行版的系统包，始终通过 FetchContent 获取
try_system_or_fetch(ftxui
    ftxui
    FETCH
        GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
        GIT_TAG        v6.1.9
        GIT_SHALLOW    TRUE
)

# --- nlohmann/json 3.12.0 - JSON 解析 ---
# Arch: pacman -S nlohmann-json
try_system_or_fetch(nlohmann_json
    nlohmann_json
    FETCH
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG        v3.12.0
        GIT_SHALLOW    TRUE
)

# --- yaml-cpp - YAML 配置文件解析 ---
# Arch: pacman -S yaml-cpp
try_system_or_fetch(yaml-cpp
    yaml-cpp
    FETCH
        GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
        GIT_TAG        yaml-cpp-0.9.0
        GIT_SHALLOW    TRUE
)

# --- GoogleTest - 单元测试 ---
# Arch: pacman -S gtest
try_system_or_fetch(GTest
    GTest
    FETCH
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.16.0
        GIT_SHALLOW    TRUE
)

# --- spdlog - 日志库 ---
# Arch: pacman -S spdlog
try_system_or_fetch(spdlog
    spdlog
    FETCH
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG        v1.15.3
        GIT_SHALLOW    TRUE
)

# 统一拉取所有需要 FetchContent 的依赖
if(_GEMUSIC_FETCH_TARGETS)
    message(STATUS "[GeMusic] FetchContent_MakeAvailable: ${_GEMUSIC_FETCH_TARGETS}")
    FetchContent_MakeAvailable(${_GEMUSIC_FETCH_TARGETS})
endif()

# --- OpenSSL - AES-CBC + RSA 加密（网易云接口加密必须）---
find_package(OpenSSL REQUIRED)

# 查找 libcurl（在 Linux 安装 libcurl4-openssl-dev 或在 Windows 通过 vcpkg 提供）
find_package(CURL REQUIRED)

# --- qrcodegen - 二维码生成（nayuki/QR-Code-generator v1.8.0）---
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
    CURL::libcurl
    $<$<PLATFORM_ID:Linux>:pthread>
)

# 编译警告（严格模式），根据不同编译器设置不同选项
if(MSVC)
    # MSVC 使用 /W4 开启较严格警告，/WX 将警告视为错误
    target_compile_options(GeMusic PRIVATE /W4 /WX)
else()
    # 对于 GCC/Clang 使用常见的警告选项
    target_compile_options(GeMusic PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

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
    CURL::libcurl
    $<$<PLATFORM_ID:Linux>:pthread>
    )

    include(GoogleTest)
    gtest_discover_tests(tests)
endif()
