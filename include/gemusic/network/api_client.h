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
    auto Get(std::string_view endpoint)
        -> std::expected<HttpResponse, AppError>;

    // 发起 POST 请求
    // 参数: endpoint - API 路径, body - 请求体 JSON
    // 返回: 成功时返回 HttpResponse，失败时返回 AppError
    auto Post(std::string_view endpoint, const nlohmann::json& body)
        -> std::expected<HttpResponse, AppError>;

    // 更新 cookies
    void SetCookies(std::string cookies);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace gemusic::network

#endif  // GEMUSIC_NETWORK_API_CLIENT_H
