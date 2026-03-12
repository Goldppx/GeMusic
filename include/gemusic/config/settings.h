#ifndef GEMUSIC_CONFIG_SETTINGS_H
#define GEMUSIC_CONFIG_SETTINGS_H

#include <cstdint>

#include <expected>
#include <string>

#include "gemusic/error.h"

namespace gemusic::config {

// 应用配置结构体，对应 YAML 配置文件中的字段
struct Settings {
    // 服务器 API 地址
    std::string api_base_url;

    // 用户认证 cookies
    std::string cookies;

    // 默认播放音量（0-100）
    int volume = 80;

    // 音频缓存目录
    std::string cache_dir = "/tmp/gemusic_cache";
};

// 从 YAML 文件加载配置
// 参数: path - 配置文件路径
// 返回: 成功时返回 Settings，失败时返回 AppError
auto LoadSettings(std::string_view path) -> std::expected<Settings, AppError>;

// 将当前配置保存到 YAML 文件
// 参数: settings - 要保存的配置, path - 保存路径
// 返回: 成功时返回 void（无值），失败时返回 AppError
auto SaveSettings(const Settings& settings, std::string_view path)
    -> std::expected<void, AppError>;

}  // namespace gemusic::config

#endif  // GEMUSIC_CONFIG_SETTINGS_H
