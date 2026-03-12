#include "gemusic/network/api_client.h"

#include <iostream>

#include <curl/curl.h>

namespace gemusic::network {

// libcurl 写回调函数，将响应数据追加到 string 中
static auto WriteCallback(void* contents, size_t size, size_t nmemb,
                          std::string* userp) -> size_t {
    const size_t total_size = size * nmemb;
    userp->append(static_cast<char*>(contents), total_size);
    return total_size;
}

// Pimpl 实现
struct ApiClient::Impl {
    std::string base_url;
    std::string cookies;

    Impl(std::string url, std::string ck)
        : base_url(std::move(url)), cookies(std::move(ck)) {
        // 全局初始化 curl（整个程序生命周期只需一次）
        curl_global_init(CURL_GLOBAL_DEFAULT);
    }

    ~Impl() { curl_global_cleanup(); }

    // 执行 HTTP 请求的通用方法
    auto DoRequest(std::string_view endpoint, const std::string& method,
                   const std::string& post_data)
        -> std::expected<HttpResponse, AppError> {
        auto* curl = curl_easy_init();
        if (curl == nullptr) {
            return std::unexpected(
                AppError{ErrorCode::kNetworkError, "初始化 curl 失败"});
        }

        // 构建完整 URL
        const std::string url = base_url + std::string(endpoint);
        std::string response_body;

        // 设置 curl 选项
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);

        // 设置 cookies
        if (!cookies.empty()) {
            curl_easy_setopt(curl, CURLOPT_COOKIE, cookies.c_str());
        }

        // POST 请求设置
        if (method == "POST") {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data.c_str());

            struct curl_slist* headers = nullptr;
            headers =
                curl_slist_append(headers, "Content-Type: application/json");
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        }

        // 执行请求
        const CURLcode res = curl_easy_perform(curl);

        if (res != CURLE_OK) {
            curl_easy_cleanup(curl);
            return std::unexpected(AppError{
                ErrorCode::kNetworkError,
                std::string("HTTP 请求失败: ") + curl_easy_strerror(res)});
        }

        // 获取状态码
        long status_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);
        curl_easy_cleanup(curl);

        // 构建响应
        HttpResponse response;
        response.status_code = static_cast<int>(status_code);
        response.body = std::move(response_body);

        // 尝试解析 JSON
        try {
            response.json = nlohmann::json::parse(response.body);
        } catch (const nlohmann::json::parse_error&) {
            // 响应不是 JSON 格式，json 字段保持为 null
        }

        return response;
    }
};

ApiClient::ApiClient(std::string base_url, std::string cookies)
    : impl_(std::make_unique<Impl>(std::move(base_url),
                                   std::move(cookies))) {}

ApiClient::~ApiClient() = default;
ApiClient::ApiClient(ApiClient&&) noexcept = default;
auto ApiClient::operator=(ApiClient&&) noexcept -> ApiClient& = default;

auto ApiClient::Get(std::string_view endpoint)
    -> std::expected<HttpResponse, AppError> {
    return impl_->DoRequest(endpoint, "GET", "");
}

auto ApiClient::Post(std::string_view endpoint, const nlohmann::json& body)
    -> std::expected<HttpResponse, AppError> {
    return impl_->DoRequest(endpoint, "POST", body.dump());
}

void ApiClient::SetCookies(std::string cookies) {
    impl_->cookies = std::move(cookies);
}

}  // namespace gemusic::network
