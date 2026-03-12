#ifndef GEMUSIC_ERROR_H
#define GEMUSIC_ERROR_H

#include <string>

namespace gemusic {

// 全局错误类型枚举
enum class ErrorCode {
    kOk,             // 无错误
    kNetworkError,   // 网络请求失败
    kParseError,     // JSON/YAML 解析错误
    kFileNotFound,   // 文件未找到
    kAuthFailed,     // 认证失败（cookies 无效等）
    kPlayerError,    // 音频播放错误
    kConfigError,    // 配置文件错误
    kUnknownError,   // 未知错误
};

// 错误信息结构体，携带错误码和描述
struct AppError {
    ErrorCode code;
    std::string message;

    AppError(ErrorCode c, std::string msg)
        : code(c), message(std::move(msg)) {}
};

}  // namespace gemusic

#endif  // GEMUSIC_ERROR_H
