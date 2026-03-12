#include "gemusic/logger.h"

#include <iostream>

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/null_sink.h>

namespace gemusic {

void InitLogger(bool verbose, std::string_view log_path) {
    if (verbose) {
        // 写入文件，debug 及以上级别，每条日志带时间戳和级别标签
        auto file_sink =
            std::make_shared<spdlog::sinks::basic_file_sink_mt>(std::string(log_path), true);
        file_sink->set_level(spdlog::level::debug);

        auto logger = std::make_shared<spdlog::logger>("gemusic", file_sink);
        logger->set_level(spdlog::level::debug);
        // 格式：[时间] [级别] 消息
        logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
        logger->flush_on(spdlog::level::debug);  // 每条都立即刷盘，方便 tail -f

        spdlog::set_default_logger(logger);

        // TUI 启动前向 stderr 打印提示（TUI 启动后 stderr 会被 FTXUI 接管）
        std::cerr << "[GeMusic] verbose 模式已开启，日志写入: " << log_path << std::endl;
        std::cerr << "[GeMusic] 可在另一终端执行: tail -f " << log_path << std::endl;
    } else {
        // null sink：零开销，所有日志静默丢弃
        auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
        auto logger = std::make_shared<spdlog::logger>("gemusic", null_sink);
        logger->set_level(spdlog::level::off);
        spdlog::set_default_logger(logger);
    }
}

}  // namespace gemusic
