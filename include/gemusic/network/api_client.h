#ifndef GEMUSIC_NETWORK_API_CLIENT_H
#define GEMUSIC_NETWORK_API_CLIENT_H

#include <cstdint>

#include <expected>
#include <memory>
#include <string>

#include <nlohmann/json.hpp>

#include "gemusic/error.h"

namespace gemusic::network {

// HTTP 响应封装
struct HttpResponse {
    int status_code;
    std::string body;
    nlohmann::json json;  // 已解析的 JSON 数据（如果响应是 JSON）
};

// API 客户端，负责与音乐服务 API 通信
// 内部使用 libcurl 发起 HTTP 请求，支持 cookies 认证
class ApiClient {
   public:
    // 构造函数
    // 参数: base_url - API 基础地址, cookies - 认证 cookies
    explicit ApiClient(std::string base_url, std::string cookies = "");

    ~ApiClient();

    // 禁止拷贝，允许移动
    ApiClient(const ApiClient&) = delete;
    auto operator=(const ApiClient&) -> ApiClient& = delete;
    ApiClient(ApiClient&&) noexcept;
    auto operator=(ApiClient&&) noexcept -> ApiClient&;

    // 发起 GET 请求
    // 参数: endpoint - API 路径（相对于 base_url）
    // 返回: 成功时返回 HttpResponse，失败时返回 AppError
    auto Get(std::string_view endpoint) -> std::expected<HttpResponse, AppError>;

    // 发起 POST 请求（通用 JSON body）
    // 参数: endpoint - API 路径, body - 请求体 JSON
    // 返回: 成功时返回 HttpResponse，失败时返回 AppError
    auto Post(std::string_view endpoint, const nlohmann::json& body)
        -> std::expected<HttpResponse, AppError>;

    // 发起网易云 Weapi 加密 POST 请求
    // 内部对 params 进行 AES+RSA Weapi 加密，使用正确的 User-Agent/Referer
    // 参数: endpoint - 完整的 Weapi URL（如 https://music.163.com/weapi/...）
    //        params  - 未加密的请求参数（JSON 对象）
    // 返回: 成功时返回 HttpResponse，失败时返回 AppError
    auto PostWeapi(std::string_view endpoint, const nlohmann::json& params)
        -> std::expected<HttpResponse, AppError>;

    // 更新 cookies
    void SetCookies(std::string cookies);

    // 从响应头中提取并追加 Set-Cookie 到当前 cookies 字符串
    // 参数: response_cookies - curl 提取到的 cookie 列表字符串
    void AppendCookies(const std::string& response_cookies);

    // 获取当前 cookies 字符串
    [[nodiscard]] auto GetCookies() const -> const std::string&;

    // 将指定 URL 的内容下载到本地文件（用于音频缓存，直接使用绝对 URL，不依赖 base_url）
    // 参数: url       - 完整的 HTTP/HTTPS 音频 URL
    //        dest_path - 本地保存路径
    // 返回: 成功时返回 void，失败时返回 AppError
    auto DownloadFile(std::string_view url, std::string_view dest_path)
        -> std::expected<void, AppError>;

   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace gemusic::network

#endif  // GEMUSIC_NETWORK_API_CLIENT_H
