#ifndef GEMUSIC_LIBRARY_LOCAL_LIBRARY_H
#define GEMUSIC_LIBRARY_LOCAL_LIBRARY_H

#include <expected>
#include <string>
#include <vector>

#include "gemusic/error.h"

namespace gemusic::library {

// 本地音频文件信息
struct LocalTrack {
    std::string file_name;  // 文件名（不含路径）
    std::string file_path;  // 完整文件路径
    std::string extension;  // 文件扩展名（小写，如 "mp3"）
};

// 扫描指定目录下的音频文件
// 参数: directory - 要扫描的目录路径（已展开的绝对路径）
// 返回: 成功时返回音频文件列表，失败时返回 AppError
// 支持的格式: mp3, flac, wav, ogg, m4a, aac, wma, opus
auto ScanLocalMusic(std::string_view directory) -> std::expected<std::vector<LocalTrack>, AppError>;

// 判断文件扩展名是否为支持的音频格式
auto IsSupportedAudioFormat(std::string_view extension) -> bool;

}  // namespace gemusic::library

#endif  // GEMUSIC_LIBRARY_LOCAL_LIBRARY_H
