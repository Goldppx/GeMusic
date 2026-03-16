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

    // 用户认证 cookies（包含 MUSIC_U 等网易云登录凭证）
    std::string cookies;

    // 已登录的用户名（登录成功后由 LoginManager 写入）
    std::string user_name;

    // 已登录用户的网易云数字 ID（登录成功后由 LoginManager 写入，用于调用歌单等 API）
    int64_t user_id = 0;

    // 默认播放音量（0-100）
    int volume = 80;

    // 音频缓存目录
    std::string cache_dir = "/tmp/gemusic_cache";

    // 本地音乐库路径，默认为用户主目录下的 Music 目录
    std::string music_library_path = "~/Music";

    // 设备唯一标识符（52 位大写十六进制，首次启动时随机生成并持久化）
    // 用于构造二维码 URL 中的 chainId 参数，绕过网易云 8821 风控
    std::string s_device_id;
    // 播放模式：0=顺序播放（默认），1=单曲循环，2=随机播放
    // 在配置文件中以整数形式保存（参见 src/settings.cpp 的注释）
    // 变更此字段会影响队列的 Next/Prev 行为
    int play_mode = 0;
    // 是否启用 MPRIS 支持（0 = 禁用，1 = 启用）
    // 启用后程序会尝试注册 MPRIS 服务到用户 session bus（需系统支持 sd-bus）
    int enable_mpris = 0;
};

// 从 YAML 文件加载配置
// 参数: path - 配置文件路径
// 返回: 成功时返回 Settings，失败时返回 AppError
auto LoadSettings(std::string_view path) -> std::expected<Settings, AppError>;
// 将当前配置保存到 YAML 文件
// 参数: settings - 要保存的配置, path - 保存路径
// 返回: 成功时返回 void（无值），失败时返回 AppError
auto SaveSettings(const Settings& settings, std::string_view path) -> std::expected<void, AppError>;

// 将路径中的 ~ 展开为用户主目录的绝对路径
auto ExpandHomePath(std::string_view path) -> std::string;

// 获取默认配置文件路径
auto GetDefaultConfigPath() -> std::string;

}  // namespace gemusic::config

#endif  // GEMUSIC_CONFIG_SETTINGS_H
