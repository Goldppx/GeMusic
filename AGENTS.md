# AGENTS.md - GeMusic 代理编码指南

本文件为在此仓库中工作的 AI 编码代理提供指南。所有注释和文档必须使用中文。

## 项目概述

- **项目名称**: GeMusic
- **类型**: C++20 TUI 音乐播放器（基于 FTXUI）
- **描述**: 通过网络接口或本地文件进行音乐播放的终端应用，支持 cookies 登录、JSON 解析、本地 YAML 配置
- **编译器**: g++
- **构建系统**: CMake
- **C++ 标准**: C++20

## 第三方依赖（均通过 FetchContent 管理）

| 库 | 用途 |
|---|------|
| FTXUI | 终端 UI 框架 |
| miniaudio | 音频播放 |
| libcurl / curlpp | HTTP 请求 |
| nlohmann/json | JSON 解析 |
| yaml-cpp | YAML 配置文件解析 |
| GoogleTest | 单元测试 |

---

## 构建 / 检查 / 测试命令

### 配置与构建

```bash
# 首次配置（Debug 模式）
cmake -B build -DCMAKE_BUILD_TYPE=Debug

# 编译整个项目
cmake --build build

# 编译特定目标
cmake --build build --target GeMusic
cmake --build build --target tests
```

### 代码检查

```bash
# 格式化检查（clang-format）
clang-format --dry-run --Werror src/**/*.cpp include/**/*.h

# 格式化修复
clang-format -i src/**/*.cpp include/**/*.h

# 静态分析（clang-tidy）
clang-tidy src/*.cpp -- -std=c++20 -I include
```

### 测试

```bash
# 运行全部测试
cd build && ctest --output-on-failure

# 运行单个测试文件（先编译测试目标，再运行）
cmake --build build --target tests
./build/tests/test_json_parser

# 通过 ctest 正则匹配运行单个测试
cd build && ctest -R "TestName" --output-on-failure

# 通过 gtest 过滤器运行单个测试用例
./build/tests/test_json_parser --gtest_filter="JsonParserTest.ParseValidJson"
```

---

## 项目目录结构

```
GeMusic/
├── CMakeLists.txt          # 顶层 CMake 配置
├── AGENTS.md               # 本文件
├── .clang-format           # 代码格式化配置
├── .clang-tidy             # 静态分析配置
├── src/                    # 源文件 (.cpp)
├── include/                # 头文件 (.h)
├── tests/                  # 测试文件
├── build/                  # 构建输出（不提交到 git）
└── config/                 # 默认配置文件模板
```

---

## 代码风格指南

### 命名规范（Google C++ 风格）

| 元素 | 规范 | 示例 |
|------|------|------|
| 类/结构体 | PascalCase | `MusicPlayer`, `HttpClient` |
| 函数 | PascalCase | `PlayTrack()`, `FetchPlaylist()` |
| 变量 | snake_case | `track_list`, `current_index` |
| 成员变量 | snake_case 加下划线后缀 | `player_state_`, `volume_` |
| 常量 | k + PascalCase | `kMaxRetries`, `kDefaultPort` |
| 枚举值 | k + PascalCase | `kPlaying`, `kPaused` |
| 宏 | UPPER_SNAKE_CASE | `GEMUSIC_VERSION` |
| 命名空间 | snake_case | `gemusic`, `gemusic::ui` |
| 文件名 | snake_case | `music_player.cpp`, `http_client.h` |

### 头文件与 include 顺序

```cpp
// 1. 对应的头文件（如果是 .cpp 文件）
#include "music_player.h"

// 2. C 系统头文件
#include <cstdint>

// 3. C++ 标准库头文件
#include <string>
#include <vector>
#include <expected>

// 4. 第三方库头文件
#include <ftxui/component/component.hpp>
#include <nlohmann/json.hpp>
#include <curl/curl.h>

// 5. 项目内头文件
#include "config/settings.h"
#include "network/api_client.h"
```

每组之间空一行，组内按字母序排列。

### 错误处理

统一使用 `std::expected` 进行错误处理，禁止使用异常控制流程：

```cpp
// 定义错误类型
enum class AppError {
    kNetworkError,
    kParseError,
    kFileNotFound,
    kAuthFailed,
};

// 函数返回 std::expected
auto FetchPlaylist(std::string_view url) -> std::expected<Playlist, AppError>;

// 调用方处理
auto result = FetchPlaylist(api_url);
if (!result) {
    // 处理错误
    std::cerr << "获取播放列表失败" << std::endl;
    return std::unexpected(result.error());
}
auto& playlist = result.value();
```

### 核心规范

- **智能指针**: 禁止裸 `new`/`delete`，必须使用 `std::unique_ptr` 或 `std::shared_ptr`
- **const 优先**: 能用 `const` / `constexpr` 的地方必须使用
- **注释语言**: 所有注释必须使用中文书写
- **FTXUI 注释**: FTXUI 相关代码必须写足够详细的中文注释，说明组件结构和交互逻辑
- **日志输出**: 使用 `std::cout` / `std::cerr` 进行日志输出
- **多线程**: 使用 `std::thread` / `std::async` 等标准库线程设施

### 格式化规则

- 使用项目根目录的 `.clang-format` 配置
- 最大行宽：建议 100 字符
- 缩进：空格（由 clang-format 统一管理）
- 大括号：与控制语句同行（K&R 风格）

---

## 测试规范

### 测试文件命名

测试文件放在 `tests/` 目录，命名格式：`test_<模块名>.cpp`

### 测试结构

```cpp
#include <gtest/gtest.h>
#include "network/api_client.h"

// 测试类命名：被测类名 + Test
class ApiClientTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 初始化测试环境
    }
};

// 测试用例命名：描述被测行为
TEST_F(ApiClientTest, ReturnsErrorOnInvalidUrl) {
    auto result = client_.Fetch("invalid://url");
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), AppError::kNetworkError);
}
```

---

## Git 提交规范（Conventional Commits）

```
<类型>(<范围>): <描述>

类型：feat, fix, docs, style, refactor, test, chore, build
范围：ui, player, network, config, auth 等模块名

示例：
  feat(player): 添加播放队列循环模式
  fix(network): 修复 cookies 过期后未刷新的问题
  docs(readme): 更新构建说明
  refactor(ui): 重构播放器界面组件结构
```
