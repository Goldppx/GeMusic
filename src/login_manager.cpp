#include "gemusic/auth/login_manager.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>

#include <spdlog/spdlog.h>
#include <qrcodegen.hpp>

#include <nlohmann/json.hpp>

#include "gemusic/config/settings.h"
#include "gemusic/network/api_client.h"

namespace gemusic::auth {

// ============================================================
// 网易云 QR 登录 API 端点（直连，无需 Node.js 代理）
// ============================================================

// 获取二维码唯一 key 的接口
static constexpr std::string_view kUnikeyEndpoint =
    "https://music.163.com/weapi/login/qrcode/unikey";

// 轮询登录状态的接口
static constexpr std::string_view kCheckLoginEndpoint =
    "https://music.163.com/weapi/login/qrcode/client/login";

// 获取用户账号信息的接口（登录成功后获取用户名）
static constexpr std::string_view kAccountEndpoint =
    "https://music.163.com/weapi/nuser/account/get";

// 轮询间隔（2 秒）
static constexpr std::chrono::seconds kPollInterval{2};

// 二维码过期时间（3 分钟，网易云 QR 有效期）
static constexpr int kQrExpireSeconds = 180;

// ============================================================
// 登录状态码（来自网易云 QR 登录 API 文档）
// ============================================================
static constexpr int kCodeExpired = 800;       // 二维码已过期
static constexpr int kCodeWaitScan = 801;      // 等待扫描
static constexpr int kCodeWaitConfirm = 802;   // 已扫描，等待确认
static constexpr int kCodeLoginSuccess = 803;  // 登录成功

// ============================================================
// Pimpl 实现
// ============================================================
struct LoginManager::Impl {
    // 外部配置引用和保存路径
    config::Settings& settings;
    std::string config_path;

    // 状态变化回调（由 UI 层注册，用于触发界面刷新）
    std::function<void()> on_state_change;

    // 当前登录状态（原子变量，多线程安全读取）
    std::atomic<LoginState> state{LoginState::kIdle};

    // 二维码矩阵（深色模块为 true）
    // 受 qr_mutex_ 保护
    std::vector<std::vector<bool>> qr_matrix;

    // 二维码目标 URL
    std::string qr_url;

    // 当前状态描述文字（受 qr_mutex_ 保护）
    std::string status_text;

    // 保护 qr_matrix、qr_url、status_text 的互斥锁
    mutable std::mutex qr_mutex;

    // 后台轮询线程
    std::thread poll_thread;

    // 控制后台线程退出的标志
    std::atomic<bool> stop_flag{false};

    // HTTP 客户端（专用于登录请求，base_url 为空因为使用完整 URL）
    network::ApiClient api_client{"", ""};

    Impl(config::Settings& s, std::string cfg_path, std::function<void()> cb)
        : settings(s), config_path(std::move(cfg_path)), on_state_change(std::move(cb)) {
        // 如果配置中已有 cookies，初始化 api_client 的 cookies
        if (!settings.cookies.empty()) {
            api_client.SetCookies(settings.cookies);
        }
    }

    ~Impl() {
        // 确保后台线程在析构时被停止
        stop_flag.store(true);
        if (poll_thread.joinable()) {
            poll_thread.join();
        }
    }

    // 更新状态并触发 UI 回调
    void SetState(LoginState new_state, std::string text = "") {
        spdlog::info("登录状态变更: {} -> \"{}\"", static_cast<int>(new_state), text);
        {
            const std::lock_guard<std::mutex> lock(qr_mutex);
            status_text = std::move(text);
        }
        state.store(new_state);
        if (on_state_change) {
            on_state_change();
        }
    }

    // 从 unikey 构造二维码 URL
    // 格式：http://music.163.com/login?codekey=<unikey>
    static auto BuildQrUrl(const std::string& unikey) -> std::string {
        return "http://music.163.com/login?codekey=" + unikey;
    }

    // 用 qrcodegen 从 URL 字符串生成二维码矩阵
    // 返回：每行为 bool 向量的二维矩阵（true = 深色模块）
    static auto GenerateQrMatrix(const std::string& url) -> std::vector<std::vector<bool>> {
        // 使用纠错等级 M（中等），适合终端显示
        const auto qr = qrcodegen::QrCode::encodeText(url.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);

        const int size = qr.getSize();
        std::vector<std::vector<bool>> matrix(static_cast<size_t>(size),
                                              std::vector<bool>(static_cast<size_t>(size), false));

        for (int row = 0; row < size; ++row) {
            for (int col = 0; col < size; ++col) {
                matrix[static_cast<size_t>(row)][static_cast<size_t>(col)] = qr.getModule(col, row);
            }
        }
        return matrix;
    }

    // 从字符串中提取指定 cookie 的值
    // 参数: cookies_str - 格式为 "NAME1=VALUE1; NAME2=VALUE2; ..." 的 cookie 字符串
    //        name       - 要提取的 cookie 名称
    // 返回: cookie 值，若不存在则返回空字符串
    static auto ExtractCookieValue(const std::string& cookies_str, std::string_view name)
        -> std::string {
        const std::string search = std::string(name) + "=";
        const auto pos = cookies_str.find(search);
        if (pos == std::string::npos) {
            return "";
        }
        const auto value_start = pos + search.size();
        const auto value_end = cookies_str.find(';', value_start);
        return (value_end == std::string::npos)
                   ? cookies_str.substr(value_start)
                   : cookies_str.substr(value_start, value_end - value_start);
    }

    // 步骤 1：向服务器请求二维码 unikey
    // 返回: unikey 字符串，失败返回空字符串
    auto FetchUnikey() -> std::string {
        SetState(LoginState::kFetchingKey, "正在获取二维码...");

        // 请求参数：type=1 表示二维码登录
        const nlohmann::json params = {{"type", 1}, {"noCheckToken", "true"}};

        auto result = api_client.PostWeapi(kUnikeyEndpoint, params);
        if (!result) {
            SetState(LoginState::kError, "获取二维码失败: " + result.error().message);
            return "";
        }

        const auto& resp = result.value();
        if (!resp.json.is_object() || !resp.json.contains("unikey")) {
            SetState(LoginState::kError, "服务器响应格式错误");
            return "";
        }

        const std::string unikey = resp.json["unikey"].get<std::string>();
        spdlog::debug("unikey 获取成功: {}", unikey);
        return unikey;
    }

    // 步骤 2：轮询登录状态
    // 每隔 kPollInterval 向服务器查询一次
    // 当收到 803（成功）时提取 cookie 并保存
    void PollLoginStatus(const std::string& unikey) {
        const nlohmann::json poll_params = {{"type", 1}, {"noCheckToken", "true"}, {"key", unikey}};

        const auto start_time = std::chrono::steady_clock::now();

        while (!stop_flag.load()) {
            // 检查是否超时（超过 kQrExpireSeconds 秒）
            const auto elapsed = std::chrono::steady_clock::now() - start_time;
            if (elapsed > std::chrono::seconds(kQrExpireSeconds)) {
                SetState(LoginState::kExpired, "二维码已过期，请重新获取");
                return;
            }

            // 发起轮询请求
            auto result = api_client.PostWeapi(kCheckLoginEndpoint, poll_params);
            if (!result) {
                // 网络错误时短暂等待后重试
                std::this_thread::sleep_for(kPollInterval);
                continue;
            }

            const auto& resp = result.value();
            if (!resp.json.is_object() || !resp.json.contains("code")) {
                std::this_thread::sleep_for(kPollInterval);
                continue;
            }

            const int code = resp.json["code"].get<int>();
            spdlog::debug("轮询响应 code: {}", code);

            if (code == kCodeExpired) {
                // 二维码过期
                SetState(LoginState::kExpired, "二维码已过期，请重新获取");
                return;
            }

            if (code == kCodeWaitScan) {
                // 等待扫描中
                SetState(LoginState::kWaitingScan, "请使用网易云 App 扫描二维码");
            } else if (code == kCodeWaitConfirm) {
                // 已扫描，等待手机端确认
                SetState(LoginState::kWaitingConfirm, "请在手机上确认登录");
            } else if (code == kCodeLoginSuccess) {
                // 登录成功，提取 MUSIC_U cookie 并保存
                OnLoginSuccess();
                return;
            }

            std::this_thread::sleep_for(kPollInterval);
        }
    }

    // 登录成功后的处理：提取 cookies、获取用户名、保存配置
    void OnLoginSuccess() {
        // 从 api_client 中获取累积的 cookies（含 MUSIC_U）
        const std::string new_cookies = api_client.GetCookies();
        spdlog::info("登录成功，cookies 长度: {}", new_cookies.size());
        settings.cookies = new_cookies;

        // 尝试获取用户名
        FetchUserName();

        // 保存配置到磁盘（持久化 MUSIC_U 和 user_name）
        auto save_result = config::SaveSettings(settings, config_path);
        if (!save_result) {
            std::cerr << "警告：保存登录配置失败: " << save_result.error().message << std::endl;
        }

        SetState(LoginState::kLoggedIn, "登录成功！欢迎，" + settings.user_name);
    }

    // 获取登录用户信息（用户名）
    // 调用 /weapi/nuser/account/get 接口
    // 返回: true 表示成功获取到有效的账户信息（code == 200）
    auto FetchUserName() -> bool {
        auto result = api_client.PostWeapi(kAccountEndpoint, nlohmann::json::object());
        if (!result) {
            return false;
        }

        const auto& resp = result.value();

        // 校验业务响应码
        if (!resp.json.is_object() || !resp.json.contains("code") ||
            resp.json["code"].get<int>() != 200) {
            spdlog::debug("FetchUserName: 账户接口响应码异常: {}", resp.json.value("code", -1));
            return false;
        }

        // 优先读 profile.nickname，备用 account.userName
        if (resp.json.contains("profile") && resp.json["profile"].is_object() &&
            resp.json["profile"].contains("nickname")) {
            settings.user_name = resp.json["profile"]["nickname"].get<std::string>();
        } else if (resp.json.contains("account") && resp.json["account"].is_object() &&
                   resp.json["account"].contains("userName")) {
            settings.user_name = resp.json["account"]["userName"].get<std::string>();
        }

        spdlog::info("获取用户名成功: {}", settings.user_name);
        return true;
    }

    // 自动登录流程：使用已保存的 cookies 校验登录状态（在后台线程中运行）
    // 策略：
    //   - API 返回 code==200 → 登录有效，进入 kLoggedIn
    //   - API 返回非 200（cookies 过期/失效）→ 清除 cookies，进入 kIdle
    //   - 网络错误 → 保留 cookies（让用户决定），进入 kIdle
    void TryAutoLoginFlow() {
        spdlog::info("开始自动登录校验，cookies 长度: {}", settings.cookies.size());
        SetState(LoginState::kVerifying, "正在验证登录状态...");

        if (stop_flag.load()) {
            return;
        }

        // 确保 api_client 使用最新的 cookies
        api_client.SetCookies(settings.cookies);

        auto result = api_client.PostWeapi(kAccountEndpoint, nlohmann::json::object());

        if (stop_flag.load()) {
            // 被 StartLogin() 或 TryAutoLogin() 中断，不修改状态
            return;
        }

        if (!result) {
            // 网络层错误（连接超时、DNS 失败等）：保留旧 cookies，让用户决定
            spdlog::warn("自动登录校验：网络错误，保留旧 cookies: {}", result.error().message);
            SetState(LoginState::kIdle, "网络错误，无法验证登录状态");
            return;
        }

        const auto& resp = result.value();

        // 判断 cookies 是否有效：code == 200 且有用户数据
        const bool cookies_valid = resp.json.is_object() && resp.json.contains("code") &&
                                   resp.json["code"].get<int>() == 200;

        if (!cookies_valid) {
            const int code = resp.json.is_object() ? resp.json.value("code", -1) : -1;
            spdlog::info("自动登录校验：cookies 已失效（code={}），清除并回到未登录状态", code);

            // cookies 明确无效，清除并持久化
            settings.cookies.clear();
            settings.user_name.clear();
            auto save_result = config::SaveSettings(settings, config_path);
            if (!save_result) {
                spdlog::warn("自动登录：清除 cookies 后保存配置失败: {}",
                             save_result.error().message);
            }
            SetState(LoginState::kIdle, "登录已过期，请重新扫码登录");
            return;
        }

        // cookies 有效，解析用户名
        if (resp.json.contains("profile") && resp.json["profile"].is_object() &&
            resp.json["profile"].contains("nickname")) {
            settings.user_name = resp.json["profile"]["nickname"].get<std::string>();
        } else if (resp.json.contains("account") && resp.json["account"].is_object() &&
                   resp.json["account"].contains("userName")) {
            settings.user_name = resp.json["account"]["userName"].get<std::string>();
        }

        spdlog::info("自动登录成功，用户: {}", settings.user_name);
        SetState(LoginState::kLoggedIn,
                 "欢迎回来，" + (settings.user_name.empty() ? "用户" : settings.user_name));
    }

    // 主登录流程（在后台线程中运行）
    void RunLoginFlow() {
        // 步骤 1：获取 unikey
        const std::string unikey = FetchUnikey();
        if (unikey.empty() || stop_flag.load()) {
            return;
        }

        // 步骤 2：构造二维码 URL 并生成矩阵
        const std::string url = BuildQrUrl(unikey);
        auto matrix = GenerateQrMatrix(url);

        {
            const std::lock_guard<std::mutex> lock(qr_mutex);
            qr_url = url;
            qr_matrix = std::move(matrix);
        }

        // 步骤 3：设置状态为等待扫描，通知 UI 刷新
        SetState(LoginState::kWaitingScan, "请使用网易云 App 扫描二维码");

        // 步骤 4：开始轮询登录状态
        PollLoginStatus(unikey);
    }
};

// ============================================================
// 公共接口实现
// ============================================================

LoginManager::LoginManager(config::Settings& settings, std::string config_path,
                           std::function<void()> on_state_change)
    : impl_(std::make_unique<Impl>(settings, std::move(config_path), std::move(on_state_change))) {}

void LoginManager::SetOnStateChange(std::function<void()> callback) {
    // 线程安全：仅在 UI 主线程调用（Run() 启动前），无需加锁
    impl_->on_state_change = std::move(callback);
}

LoginManager::~LoginManager() = default;
LoginManager::LoginManager(LoginManager&&) noexcept = default;
auto LoginManager::operator=(LoginManager&&) noexcept -> LoginManager& = default;

void LoginManager::StartLogin() {
    // 如果已有后台线程在运行，先停止
    impl_->stop_flag.store(true);
    if (impl_->poll_thread.joinable()) {
        impl_->poll_thread.join();
    }

    // 重置停止标志和状态
    impl_->stop_flag.store(false);

    // 清空旧的二维码数据
    {
        const std::lock_guard<std::mutex> lock(impl_->qr_mutex);
        impl_->qr_matrix.clear();
        impl_->qr_url.clear();
    }

    // 在新线程中运行完整登录流程
    impl_->poll_thread = std::thread([this] { impl_->RunLoginFlow(); });
}

void LoginManager::TryAutoLogin() {
    // cookies 为空时无需校验，直接返回
    if (impl_->settings.cookies.empty()) {
        spdlog::debug("TryAutoLogin: cookies 为空，跳过自动登录");
        return;
    }

    // 停止当前可能正在运行的后台线程（如重复调用）
    impl_->stop_flag.store(true);
    if (impl_->poll_thread.joinable()) {
        impl_->poll_thread.join();
    }

    // 重置停止标志，启动校验线程
    impl_->stop_flag.store(false);
    impl_->poll_thread = std::thread([this] { impl_->TryAutoLoginFlow(); });
}

void LoginManager::Logout() {
    // 停止后台轮询线程
    impl_->stop_flag.store(true);
    if (impl_->poll_thread.joinable()) {
        impl_->poll_thread.join();
    }

    // 清除登录状态
    impl_->settings.cookies.clear();
    impl_->settings.user_name.clear();
    impl_->api_client.SetCookies("");

    {
        const std::lock_guard<std::mutex> lock(impl_->qr_mutex);
        impl_->qr_matrix.clear();
        impl_->qr_url.clear();
        impl_->status_text.clear();
    }

    // 保存清除后的配置到磁盘
    auto save_result = config::SaveSettings(impl_->settings, impl_->config_path);
    if (!save_result) {
        std::cerr << "警告：保存退出登录配置失败: " << save_result.error().message << std::endl;
    }

    impl_->SetState(LoginState::kIdle, "已退出登录");
}

auto LoginManager::GetState() const -> LoginState {
    return impl_->state.load();
}

auto LoginManager::GetQrMatrix() const -> std::vector<std::vector<bool>> {
    // 在锁保护下复制矩阵，避免与后台生成线程的数据竞争
    const std::lock_guard<std::mutex> lock(impl_->qr_mutex);
    return impl_->qr_matrix;
}

auto LoginManager::GetStatusText() const -> std::string {
    const std::lock_guard<std::mutex> lock(impl_->qr_mutex);
    return impl_->status_text;
}

auto LoginManager::GetQrUrl() const -> std::string {
    const std::lock_guard<std::mutex> lock(impl_->qr_mutex);
    return impl_->qr_url;
}

}  // namespace gemusic::auth
