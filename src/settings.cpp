#include "gemusic/config/settings.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

#include <yaml-cpp/yaml.h>

namespace gemusic::config {

auto ExpandHomePath(std::string_view path) -> std::string {
    auto path_str = std::string(path);
    if (path_str.starts_with("~/")) {
        // 获取用户主目录
        const char* home = std::getenv("HOME");
        if (home != nullptr) {
            path_str.replace(0, 1, home);
        }
    }
    return path_str;
}

auto GetDefaultConfigPath() -> std::string {
    return ExpandHomePath("~/.config/GeMusic/config.yaml");
}

auto LoadSettings(std::string_view path) -> std::expected<Settings, AppError> {
    try {
        // 先检查文件是否存在
        if (!std::filesystem::exists(std::string(path))) {
            return std::unexpected(
                AppError{ErrorCode::kFileNotFound, "配置文件不存在: " + std::string(path)});
        }

        // 加载 YAML 配置文件
        YAML::Node config = YAML::LoadFile(std::string(path));

        Settings settings;

        // 读取各字段，缺失时使用默认值
        if (config["api_base_url"]) {
            settings.api_base_url = config["api_base_url"].as<std::string>();
        }
        if (config["cookies"]) {
            settings.cookies = config["cookies"].as<std::string>();
        }
        if (config["user_name"]) {
            settings.user_name = config["user_name"].as<std::string>();
        }
        if (config["user_id"]) {
            settings.user_id = config["user_id"].as<int64_t>();
        }
        if (config["volume"]) {
            settings.volume = config["volume"].as<int>();
        }
        if (config["cache_dir"]) {
            settings.cache_dir = config["cache_dir"].as<std::string>();
        }
        if (config["music_library_path"]) {
            settings.music_library_path = config["music_library_path"].as<std::string>();
        }
        if (config["s_device_id"]) {
            settings.s_device_id = config["s_device_id"].as<std::string>();
        }
        // play_mode 存为整数：0=顺序,1=单曲循环,2=随机（若缺失则保持默认 0）
        if (config["play_mode"]) {
            try {
                settings.play_mode = config["play_mode"].as<int>();
            } catch (...) {
                settings.play_mode = 0;
            }
        }
        // enable_mpris 存为整数：0=禁用,1=启用（若缺失则默认禁用）
        if (config["enable_mpris"]) {
            try {
                settings.enable_mpris = config["enable_mpris"].as<int>();
            } catch (...) {
                settings.enable_mpris = 0;
            }
        }

        return settings;
    } catch (const YAML::Exception& e) {
        return std::unexpected(
            AppError{ErrorCode::kConfigError, std::string("配置文件解析失败: ") + e.what()});
    } catch (const std::exception& e) {
        return std::unexpected(
            AppError{ErrorCode::kFileNotFound, std::string("无法读取配置文件: ") + e.what()});
    }
}

auto SaveSettings(const Settings& settings, std::string_view path)
    -> std::expected<void, AppError> {
    try {
        // 构建 YAML 节点
        YAML::Emitter out;
        out << YAML::BeginMap;
        out << YAML::Key << "api_base_url" << YAML::Value << settings.api_base_url;
        out << YAML::Key << "cookies" << YAML::Value << settings.cookies;
        out << YAML::Key << "user_name" << YAML::Value << settings.user_name;
        out << YAML::Key << "user_id" << YAML::Value << settings.user_id;
        out << YAML::Key << "volume" << YAML::Value << settings.volume;
        out << YAML::Key << "cache_dir" << YAML::Value << settings.cache_dir;
        out << YAML::Key << "music_library_path" << YAML::Value << settings.music_library_path;
        out << YAML::Key << "s_device_id" << YAML::Value << settings.s_device_id;
        // play_mode: 0=顺序,1=单曲循环,2=随机（存为整数，便于外部脚本和老配置兼容）
        out << YAML::Key << "play_mode" << YAML::Value << settings.play_mode;
        // enable_mpris: 0=禁用,1=启用（启用时程序会尝试注册 MPRIS 服务到用户 session bus）
        out << YAML::Key << "enable_mpris" << YAML::Value << settings.enable_mpris;
        out << YAML::EndMap;

        // 确保父目录存在（首次运行时 ~/.config/GeMusic/ 可能尚未创建）
        std::filesystem::create_directories(std::filesystem::path(std::string(path)).parent_path());

        // 写入文件（在文件顶部添加 play_mode 的注释说明）
        std::ofstream fout{std::string(path)};
        if (!fout.is_open()) {
            return std::unexpected(
                AppError{ErrorCode::kFileNotFound,
                         std::string("无法打开配置文件进行写入: ") + std::string(path)});
        }
        // 在配置顶部写注释，说明 play_mode 的取值含义（保持为整数）
        fout << "# play_mode: 0=顺序, 1=单曲循环, 2=随机 (请勿改为字符串)\n";
        fout << out.c_str();
        return {};
    } catch (const std::exception& e) {
        return std::unexpected(
            AppError{ErrorCode::kConfigError, std::string("保存配置文件失败: ") + e.what()});
    }
}

}  // namespace gemusic::config
