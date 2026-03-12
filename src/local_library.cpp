#include "gemusic/library/local_library.h"

#include <algorithm>
#include <filesystem>
#include <string>

namespace gemusic::library {

namespace fs = std::filesystem;

// 支持的音频文件扩展名集合
static const std::vector<std::string> kSupportedExtensions = {
    ".mp3", ".flac", ".wav", ".ogg", ".m4a", ".aac", ".wma", ".opus",
};

auto IsSupportedAudioFormat(std::string_view extension) -> bool {
    auto ext = std::string(extension);
    // 统一转为小写比较
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return std::find(kSupportedExtensions.begin(), kSupportedExtensions.end(), ext) !=
           kSupportedExtensions.end();
}

auto ScanLocalMusic(std::string_view directory)
    -> std::expected<std::vector<LocalTrack>, AppError> {
    const auto dir_path = fs::path(std::string(directory));

    // 检查目录是否存在
    if (!fs::exists(dir_path)) {
        return std::unexpected(
            AppError{ErrorCode::kFileNotFound, "音乐库目录不存在: " + std::string(directory)});
    }

    if (!fs::is_directory(dir_path)) {
        return std::unexpected(
            AppError{ErrorCode::kFileNotFound, "路径不是目录: " + std::string(directory)});
    }

    std::vector<LocalTrack> tracks;

    // 递归遍历目录下所有文件
    for (const auto& entry : fs::recursive_directory_iterator(
             dir_path, fs::directory_options::skip_permission_denied)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        const auto& path = entry.path();
        auto ext = path.extension().string();

        // 检查是否为支持的音频格式
        if (IsSupportedAudioFormat(ext)) {
            LocalTrack track;
            track.file_name = path.filename().string();
            track.file_path = path.string();
            // 去掉扩展名开头的点号，转小写
            if (!ext.empty() && ext[0] == '.') {
                ext = ext.substr(1);
            }
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            track.extension = ext;
            tracks.push_back(std::move(track));
        }
    }

    // 按文件名排序
    std::sort(tracks.begin(), tracks.end(),
              [](const LocalTrack& a, const LocalTrack& b) { return a.file_name < b.file_name; });

    return tracks;
}

}  // namespace gemusic::library
