#ifndef GEMUSIC_LOGGER_H
#define GEMUSIC_LOGGER_H

#include <string_view>

// spdlog 全局默认 logger 的薄封装
// 使用方：直接调用 spdlog::debug() / spdlog::info() / spdlog::warn() / spdlog::error()
// 在调用任何日志函数之前，main() 必须先调用 gemusic::InitLogger()
#include <spdlog/spdlog.h>

namespace gemusic {

// 初始化全局 logger
// verbose=false 时：安装 null sink，所有日志静默丢弃（零开销）
// verbose=true  时：安装 basic_file_sink，将 debug 及以上级别写入 log_path
//                   同时在 TUI 启动前向 stderr 打印一行提示
void InitLogger(bool verbose, std::string_view log_path = "/tmp/gemusic_debug.log");

}  // namespace gemusic

#endif  // GEMUSIC_LOGGER_H
