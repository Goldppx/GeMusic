#include "gemusic/network/api_client.h"

#include <iostream>

#include <curl/curl.h>
#include <spdlog/spdlog.h>

#include "gemusic/network/netease_crypto.h"

namespace gemusic::network {

// libcurl 响应体写回调：将数据追加到 string 中
static auto WriteBodyCallback(void* contents, size_t size, size_t nmemb, std::string* userp)
    -> size_t {
    const size_t total_size = size * nmemb;
    userp->append(static_cast<char*>(contents), total_size);
    return total_size;
}

// libcurl 响应头写回调：将头部行追加到 string 中
// 用于提取 Set-Cookie 字段
static auto WriteHeaderCallback(void* contents, size_t size, size_t nmemb, std::string* userp)
    -> size_t {
    const size_t total_size = size * nmemb;
    userp->append(static_cast<char*>(contents), total_size);
    return total_size;
}

// Pimpl 实现
struct ApiClient::Impl {
    std::string base_url;
    std::string cookies;

    Impl(std::string url, std::string ck) : base_url(std::move(url)), cookies(std::move(ck)) {
        // 全局初始化 curl（整个程序生命周期只需一次）
        curl_global_init(CURL_GLOBAL_DEFAULT);
    }

    ~Impl() {
        curl_global_cleanup();
    }

    // 执行通用 HTTP 请求（GET 或 JSON POST）
    auto DoRequest(std::string_view endpoint, const std::string& method,
                   const std::string& post_data) -> std::expected<HttpResponse, AppError> {
        auto* curl = curl_easy_init();
        if (curl == nullptr) {
            return std::unexpected(AppError{ErrorCode::kNetworkError, "初始化 curl 失败"});
        }

        // 构建完整 URL
        const std::string url = base_url + std::string(endpoint);
        std::string response_body;

        // 设置基本 curl 选项
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteBodyCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);

        // 设置 cookies
        if (!cookies.empty()) {
            curl_easy_setopt(curl, CURLOPT_COOKIE, cookies.c_str());
        }

        // POST 请求设置
        if (method == "POST") {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data.c_str());

            struct curl_slist* headers = nullptr;
            headers = curl_slist_append(headers, "Content-Type: application/json");
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        }

        // 执行请求
        const CURLcode res = curl_easy_perform(curl);

        if (res != CURLE_OK) {
            curl_easy_cleanup(curl);
            spdlog::error("HTTP 请求失败 [{}]: {}", url, curl_easy_strerror(res));
            return std::unexpected(
                AppError{ErrorCode::kNetworkError,
                         std::string("HTTP 请求失败: ") + curl_easy_strerror(res)});
        }

        // 获取状态码
        long status_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);
        curl_easy_cleanup(curl);

        spdlog::debug("{} {} -> HTTP {}", method, url, status_code);

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

    // 执行网易云 Weapi 加密 POST 请求
    // endpoint 为完整 URL（如 https://music.163.com/weapi/...）
    // 此方法负责：
    //   1. 对 params 进行 Weapi 双层 AES + RSA 加密
    //   2. 设置网易云要求的 User-Agent、Referer、Content-Type 请求头
    //   3. 携带 cookies（包含 sDeviceId、os、appver 等设备信息）
    //   4. 从响应头中收集 Set-Cookie（用于后续更新 MUSIC_U）
    auto DoWeapiRequest(std::string_view endpoint, const nlohmann::json& params)
        -> std::expected<HttpResponse, AppError> {
        auto* curl = curl_easy_init();
        if (curl == nullptr) {
            return std::unexpected(AppError{ErrorCode::kNetworkError, "初始化 curl 失败"});
        }

        // 对请求参数进行 Weapi 加密
        const auto payload = EncryptWeapi(params);
        const std::string post_body = payload.ToPostBody();

        std::string response_body;
        std::string response_headers;

        // 设置请求 URL
        curl_easy_setopt(curl, CURLOPT_URL, std::string(endpoint).c_str());
        spdlog::debug("Weapi POST -> {}", endpoint);

        // 跟随 HTTP 重定向（网易云部分接口会 302）
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

        // 自动处理 gzip/deflate 压缩（空字符串表示接受所有支持的编码）
        curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");

        // 设置写回调
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteBodyCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, WriteHeaderCallback);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response_headers);

        // 开启 cookie 引擎（允许接收并存储 Set-Cookie）
        curl_easy_setopt(curl, CURLOPT_COOKIEFILE, "");

        // 设置 POST 请求体
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_body.c_str());

        // 构建网易云要求的请求头
        // User-Agent 模拟 iOS 网易云客户端，以获取正确的响应
        struct curl_slist* headers = nullptr;
        headers =
            curl_slist_append(headers,
                              "User-Agent: Mozilla/5.0 (iPhone; CPU iPhone OS 17_0 like Mac OS X) "
                              "AppleWebKit/605.1.15 (KHTML, like Gecko) Mobile/15E148 "
                              "CloudMusic/0.1.1 NeteaseMusic");
        headers = curl_slist_append(headers, "Referer: https://music.163.com");
        headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");

        // 构建 cookies 字符串（包含设备信息和已有的认证 cookie）
        std::string cookie_str = cookies;
        // 确保基本设备 cookie 存在（若未在 cookies 中设置则追加默认值）
        if (cookie_str.find("os=") == std::string::npos) {
            if (!cookie_str.empty()) {
                cookie_str += "; ";
            }
            cookie_str += "os=ios; appver=9.0.65; __remember_me=true";
        }

        if (!cookie_str.empty()) {
            curl_easy_setopt(curl, CURLOPT_COOKIE, cookie_str.c_str());
        }

        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        // 执行请求
        const CURLcode res = curl_easy_perform(curl);
        curl_slist_free_all(headers);

        if (res != CURLE_OK) {
            curl_easy_cleanup(curl);
            spdlog::error("Weapi 请求失败 [{}]: {}", endpoint, curl_easy_strerror(res));
            return std::unexpected(
                AppError{ErrorCode::kNetworkError,
                         std::string("Weapi 请求失败: ") + curl_easy_strerror(res)});
        }

        // 获取状态码
        long status_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);
        curl_easy_cleanup(curl);

        spdlog::debug("Weapi {} -> HTTP {}", endpoint, status_code);

        // 构建响应对象
        HttpResponse response;
        response.status_code = static_cast<int>(status_code);
        response.body = std::move(response_body);

        // 从响应头中提取 Set-Cookie 并更新 cookies
        // 格式：每行可能为 "Set-Cookie: NAME=VALUE; ..."
        ExtractAndUpdateCookies(response_headers);

        // 尝试解析 JSON
        try {
            response.json = nlohmann::json::parse(response.body);
            // 记录业务响应码（网易云 API 均有 code 字段）
            if (response.json.is_object() && response.json.contains("code")) {
                spdlog::debug("Weapi {} 业务码: {}", endpoint, response.json["code"].get<int>());
            }
        } catch (const nlohmann::json::parse_error&) {
            // 响应不是 JSON 格式，json 字段保持为 null
            spdlog::warn("Weapi {} 响应非 JSON，body 长度: {}", endpoint, response.body.size());
        }

        return response;
    }

    // 从响应头字符串中提取 Set-Cookie 行，并将新 cookie 合并到 cookies_ 中
    // 参数: response_headers - 完整的响应头字符串（每行以 "\r\n" 结尾）
    void ExtractAndUpdateCookies(const std::string& response_headers) {
        // 逐行扫描响应头，提取 Set-Cookie 字段
        std::string::size_type pos = 0;
        while (pos < response_headers.size()) {
            const auto line_end = response_headers.find("\r\n", pos);
            const auto line = (line_end == std::string::npos)
                                  ? response_headers.substr(pos)
                                  : response_headers.substr(pos, line_end - pos);

            // 忽略大小写匹配 "Set-Cookie:"
            if (line.size() > 11) {
                std::string prefix = line.substr(0, 11);
                for (auto& c : prefix) {
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                }
                if (prefix == "set-cookie:") {
                    // 提取 cookie 名称和值（到第一个 ';' 之前的部分）
                    const auto cookie_start = line.find_first_not_of(' ', 11);
                    if (cookie_start != std::string::npos) {
                        const auto semicolon_pos = line.find(';', cookie_start);
                        const std::string cookie_kv =
                            (semicolon_pos == std::string::npos)
                                ? line.substr(cookie_start)
                                : line.substr(cookie_start, semicolon_pos - cookie_start);

                        // 将新 cookie 追加到 cookies 字符串
                        if (!cookies.empty()) {
                            cookies += "; ";
                        }
                        cookies += cookie_kv;
                    }
                }
            }

            if (line_end == std::string::npos) {
                break;
            }
            pos = line_end + 2;  // 跳过 "\r\n"
        }
    }
};

ApiClient::ApiClient(std::string base_url, std::string cookies)
    : impl_(std::make_unique<Impl>(std::move(base_url), std::move(cookies))) {}

ApiClient::~ApiClient() = default;
ApiClient::ApiClient(ApiClient&&) noexcept = default;
auto ApiClient::operator=(ApiClient&&) noexcept -> ApiClient& = default;

auto ApiClient::Get(std::string_view endpoint) -> std::expected<HttpResponse, AppError> {
    return impl_->DoRequest(endpoint, "GET", "");
}

auto ApiClient::Post(std::string_view endpoint, const nlohmann::json& body)
    -> std::expected<HttpResponse, AppError> {
    return impl_->DoRequest(endpoint, "POST", body.dump());
}

auto ApiClient::PostWeapi(std::string_view endpoint, const nlohmann::json& params)
    -> std::expected<HttpResponse, AppError> {
    return impl_->DoWeapiRequest(endpoint, params);
}

void ApiClient::SetCookies(std::string cookies) {
    impl_->cookies = std::move(cookies);
}

void ApiClient::AppendCookies(const std::string& response_cookies) {
    impl_->ExtractAndUpdateCookies(response_cookies);
}

auto ApiClient::GetCookies() const -> const std::string& {
    return impl_->cookies;
}

}  // namespace gemusic::network
