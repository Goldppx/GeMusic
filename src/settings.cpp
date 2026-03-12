#include "gemusic/config/settings.h"

#include <fstream>
#include <iostream>

#include <yaml-cpp/yaml.h>

namespace gemusic::config {

auto LoadSettings(std::string_view path) -> std::expected<Settings, AppError> {
    try {
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
        if (config["volume"]) {
            settings.volume = config["volume"].as<int>();
        }
        if (config["cache_dir"]) {
            settings.cache_dir = config["cache_dir"].as<std::string>();
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
        out << YAML::Key << "volume" << YAML::Value << settings.volume;
        out << YAML::Key << "cache_dir" << YAML::Value << settings.cache_dir;
        out << YAML::EndMap;

        // 写入文件
        std::ofstream fout{std::string(path)};
        if (!fout.is_open()) {
            return std::unexpected(
                AppError{ErrorCode::kFileNotFound,
                         std::string("无法打开配置文件进行写入: ") + std::string(path)});
        }
        fout << out.c_str();
        return {};
    } catch (const std::exception& e) {
        return std::unexpected(
            AppError{ErrorCode::kConfigError, std::string("保存配置文件失败: ") + e.what()});
    }
}

}  // namespace gemusic::config
