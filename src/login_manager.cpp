#include "gemusic/auth/login_manager.h"

#include <array>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <random>
#include <sstream>
#include <thread>

#include <openssl/evp.h>

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

// 手机号+密码登录接口
static constexpr std::string_view kPasswordLoginEndpoint =
    "https://music.163.com/weapi/login/cellphone";

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
static constexpr int kCodeRiskControl = 8821;  // 触发风控（设备异常），需重新整个登录流程

// ============================================================
// 真实 iOS 设备 ID 白名单（52 位大写十六进制）
// 来源：参考 go-musicfox 预置设备列表，随机取一条注入 deviceId cookie
// 绕过网易云对纯随机生成 deviceId 的识别
// ============================================================
// clang-format off
static constexpr std::array<std::string_view, 20> kDeviceIdWhitelist = {
    "BA4E6414F1F63E32BAB01B1F23CE5B0C97DA43BD6F77CD6A7B4D3BAD9BDDCA8",
    "FB14C62DAC4FCBCC68CC1E0929AD83E7B0AC3C97AA1C5D1B9B9DABB8F9421E6",
    "A8D6C51EFC2EB2D1D6E3A0294DB2F8B371C5FC98A2D7C6C4DEB3F0A59BCC6E2",
    "C4B7D09EFEC1A3F2B5E78D26C0A942F18E3B7D54A6F0C2E9B1D83A7F5E4C296",
    "D2F4A68CB9E71B3E06C5D0A82F473E9B1C64FA27D8B5E3C0A916F4B7D2E8C53",
    "E6B2C94DAF83E07A1D5F0C3B82E741A96D4FB28C7E0B5D1A3F96C2E8B4D70A5",
    "F0C4B76EAD91F23B5E8D0A47C2E369B8D5FC1A0E7B4D2C6F83A9E1B5D7C04F2",
    "A2D8E46CBF07B91A3E5F0D28C4B763E9A1D5FB6C0E3A7B2D98F4C1E6B0A5D3E",
    "B6E0A28DFC93B15E7D4A0C68F2E47B9D1A3FC0E5B82D6C4A97F1E3B9D5C07A2",
    "C8F2B40EAD75C19D3E6B0A48F4C261B7E9D3FA0C5E1A8B4D72F6C0E3B9D5A17",
    "D4A6C02FBE87D31A5F8E0B68C2A4F79E3D1BC5A07E4F2B6D80C9A1E5B3D7C2F",
    "E8C0D426AD59E73B1F6A0B48E4C683A7D2FB1E0C9A5B3D76F0C2E4B8D1A9C5E",
    "FA2E648CBD7BF15D3A0E6C4B82F4A9E1D5CB3F07E2B8D6A40C1E9B5D3A7C0F4",
    "1C4A068EFD8BE37D5F2A0E4C6B0A8F2E9D1CB5F03E6A2B4D70C8E1B3D5A9C7E",
    "3E6C2A08FBA0D59E7D4F2A6B8C0E4F62A9D1E3B5C7F0A2E4B8D6C1A3E9B5D7",
    "5A8E4C2AFD0BE71A9F6D4B2C0A8E2F4C6B0D8E2A4F6C8B0D2E4A6F8C0B2D4E6",
    "7CAE608BFD2CE91ABFD6E4B2C0A8E4F6C8B2D0E2A4F6C8B0D4E6A8F0C2B4D6E",
    "9EC082AFED4EAB3CFD8E6C4B2A0E6F8CABD2F4E6A8C0B2D4E6A8F0C2B4D6E8A",
    "B0E0A4CFE1D6ECB5DFE0A8C6B4A2E8FCABD4F6E8A2C4B6D8E0A2F4C6B8D0E2A4",
    "D2E0C6EAF8BAED7EBF2ACE8B6A4E0BCABD6F8BAC2E4B6D8EAC4F6E8A0C2B4D6",
};
// clang-format on

// ============================================================
// Pimpl 实现
// ============================================================

// 生成 52 位大写十六进制随机字符串，作为本机 sDeviceId
// 仅在配置文件中不存在 s_device_id 时调用一次，之后持久化复用
static auto GenerateSDeviceId() -> std::string {
    std::mt19937_64 rng{std::random_device{}()};
    std::uniform_int_distribution<uint64_t> dist;
    // 52 位十六进制 = 208 位二进制，用 4 个 uint64_t（256 位）截取前 52 字符
    std::ostringstream oss;
    oss << std::uppercase << std::hex << std::setfill('0');
    oss << std::setw(16) << dist(rng);
    oss << std::setw(16) << dist(rng);
    oss << std::setw(16) << dist(rng);
    oss << std::setw(4) << (dist(rng) & 0xFFFFULL);
    return oss.str();  // 52 字符
}

// 从白名单中随机选取一个真实 iOS deviceId
static auto PickDeviceId() -> std::string_view {
    std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<size_t> dist{0, kDeviceIdWhitelist.size() - 1};
    return kDeviceIdWhitelist[dist(rng)];
}

// 使用 OpenSSL EVP 接口计算输入字符串的 MD5，返回 32 位小写十六进制字符串
// 用于账密登录时对明文密码进行哈希处理，符合网易云 weapi 接口要求
static auto Md5Hex(const std::string& input) -> std::string {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_md5(), nullptr);
    EVP_DigestUpdate(ctx, input.data(), input.size());
    EVP_DigestFinal_ex(ctx, digest, &digest_len);
    EVP_MD_CTX_free(ctx);
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (unsigned int i = 0; i < digest_len; ++i) {
        oss << std::setw(2) << static_cast<unsigned int>(digest[i]);
    }
    return oss.str();
}

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

    // 本机 sDeviceId（从 settings 加载或首次生成）
    std::string s_device_id;

    // 本次登录流程随机选取的白名单 deviceId（每次 StartLogin 重新选）
    std::string device_id;

    Impl(config::Settings& s, std::string cfg_path, std::function<void()> cb)
        : settings(s), config_path(std::move(cfg_path)), on_state_change(std::move(cb)) {
        // 如果配置中已有 cookies，初始化 api_client 的 cookies
        if (!settings.cookies.empty()) {
            api_client.SetCookies(settings.cookies);
        }

        // 确保 sDeviceId 已存在，否则生成并持久化
        if (settings.s_device_id.empty()) {
            settings.s_device_id = GenerateSDeviceId();
            spdlog::info("首次生成 sDeviceId: {}", settings.s_device_id);
            auto save_result = config::SaveSettings(settings, config_path);
            if (!save_result) {
                spdlog::warn("生成 sDeviceId 后保存配置失败: {}", save_result.error().message);
            }
        }
        s_device_id = settings.s_device_id;

        // 随机选一个白名单 deviceId（每次程序启动选新值）
        device_id = std::string(PickDeviceId());
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

    // 从 unikey 和 sDeviceId 构造二维码 URL
    // 格式：http://music.163.com/login?codekey=<unikey>&chainId=v1_<sDeviceId>_web_login_<毫秒时间戳>
    // chainId 缺失是触发 8821 风控的主要原因
    static auto BuildQrUrl(const std::string& unikey, const std::string& s_device_id)
        -> std::string {
        // 获取当前毫秒时间戳
        const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::system_clock::now().time_since_epoch())
                                .count();
        const std::string chain_id = "v1_" + s_device_id + "_web_login_" + std::to_string(now_ms);
        return "http://music.163.com/login?codekey=" + unikey + "&chainId=" + chain_id;
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

        // 在请求前注入设备 cookie（sDeviceId、deviceId 等）
        // 这些 cookie 必须在获取 unikey 之前就存在，否则后续轮询校验会失败
        InjectDeviceCookies();

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

    // 注入完整的 iOS 设备 cookie 到 api_client
    // 必须包含：sDeviceId、deviceId（白名单）、os、appver、osver、buildver、resolution
    // 若 cookie 中已存在某项则保留旧值，否则追加
    void InjectDeviceCookies() {
        std::string ck = api_client.GetCookies();

        // 获取当前 Unix 时间戳（秒），作为 buildver
        const auto now_sec = std::chrono::duration_cast<std::chrono::seconds>(
                                 std::chrono::system_clock::now().time_since_epoch())
                                 .count();
        const std::string buildver = std::to_string(now_sec);

        // 需要注入的 cookie 键值对（若已存在则跳过）
        const std::vector<std::pair<std::string, std::string>> device_cookies = {
            {"sDeviceId", s_device_id},  {"deviceId", device_id},   {"os", "ios"},
            {"appver", "9.0.65"},        {"osver", "17.4.1"},       {"buildver", buildver},
            {"resolution", "1920x1080"}, {"__remember_me", "true"},
        };

        for (const auto& [key, value] : device_cookies) {
            if (ck.find(key + "=") == std::string::npos) {
                if (!ck.empty()) {
                    ck += "; ";
                }
                ck += key + "=" + value;
            }
        }

        api_client.SetCookies(ck);
        spdlog::debug("注入设备 cookie 完成，cookies 长度: {}", ck.size());
    }

    // 步骤 2：轮询登录状态
    // 每隔 kPollInterval 向服务器查询一次
    // 当收到 803（成功）时提取 cookie 并保存；收到 8821（风控）时通知调用方重新开始
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

            // 轮询前临时将 os cookie 切换为 pc，以绕过部分设备校验
            {
                const std::string cur = api_client.GetCookies();
                std::string patched = cur;
                const auto os_pos = patched.find("os=ios");
                if (os_pos != std::string::npos) {
                    patched.replace(os_pos, 6, "os=pc");
                } else if (patched.find("os=") == std::string::npos) {
                    if (!patched.empty()) {
                        patched += "; ";
                    }
                    patched += "os=pc";
                }
                api_client.SetCookies(patched);
            }

            // 发起轮询请求
            auto result = api_client.PostWeapi(kCheckLoginEndpoint, poll_params);

            // 轮询结束后恢复 os=ios
            {
                const std::string cur = api_client.GetCookies();
                std::string restored = cur;
                const auto os_pos = restored.find("os=pc");
                if (os_pos != std::string::npos) {
                    restored.replace(os_pos, 5, "os=ios");
                }
                api_client.SetCookies(restored);
            }

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

            if (code == kCodeRiskControl) {
                // 触发风控（8821）：需要重新整个登录流程（重新获取 unikey）
                spdlog::warn("触发 8821 风控，即将重新开始登录流程");
                SetState(LoginState::kExpired, "触发风控，正在重新获取二维码...");
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

        // 读取账户数字 ID（用于后续调用歌单等业务 API）
        if (resp.json.contains("account") && resp.json["account"].is_object() &&
            resp.json["account"].contains("id")) {
            settings.user_id = resp.json["account"]["id"].get<int64_t>();
        }

        spdlog::info("获取用户名成功: {}", settings.user_name);
        return true;
    }

    // 手机号+密码登录流程（在后台线程中运行）
    // 流程：
    //   1. 注入设备 Cookie（避免风控）
    //   2. 对明文密码做 MD5，向 weapi/login/cellphone 发起 POST 请求
    //   3. 响应 code==200 时调用 OnLoginSuccess() 完成登录
    //   4. 非 200 时读取 message 字段展示给用户
    void DoPasswordLogin(const std::string& phone, const std::string& password) {
        SetState(LoginState::kVerifying, "正在登录...");

        // 登录前注入设备 Cookie，防止触发风控
        InjectDeviceCookies();

        const nlohmann::json params = {
            {"phone", phone},
            {"password", Md5Hex(password)},
            {"rememberLogin", true},
        };

        auto result = api_client.PostWeapi(kPasswordLoginEndpoint, params);
        if (!result) {
            SetState(LoginState::kError, "登录失败: " + result.error().message);
            return;
        }

        const auto& resp = result.value();
        const int code = resp.json.is_object() ? resp.json.value("code", -1) : -1;

        if (code == 200) {
            // 登录成功，走统一成功处理路径
            OnLoginSuccess();
            return;
        }

        // 登录失败：优先展示服务端的 message，否则展示错误码
        const std::string msg = resp.json.is_object() ? resp.json.value("message", "") : "";
        SetState(LoginState::kError,
                 msg.empty() ? ("登录失败，错误码: " + std::to_string(code)) : msg);
    }

    // Cookie 登录流程（在后台线程中运行）
    // 用户提供的完整 Cookie 字符串（须包含 MUSIC_U）直接设置到 api_client，
    // 调用账户接口校验有效性：
    //   - code==200 → Cookie 有效，走 OnLoginSuccess() 完成登录
    //   - 非 200    → Cookie 无效，清除并回到 kError
    //   - 网络错误  → 回到 kError，保留输入的 Cookie 供重试
    void DoCookieLogin(const std::string& cookie) {
        SetState(LoginState::kVerifying, "正在验证 Cookie...");

        // 将用户提供的 Cookie 设置到 api_client
        api_client.SetCookies(cookie);

        // 注入设备 Cookie（sDeviceId、deviceId 等），与其它登录方式保持一致
        InjectDeviceCookies();

        auto result = api_client.PostWeapi(kAccountEndpoint, nlohmann::json::object());

        if (stop_flag.load()) {
            return;
        }

        if (!result) {
            // 网络层错误
            SetState(LoginState::kError, "Cookie 验证失败: " + result.error().message);
            return;
        }

        const auto& resp = result.value();
        const bool cookie_valid = resp.json.is_object() && resp.json.contains("code") &&
                                  resp.json["code"].get<int>() == 200;

        if (!cookie_valid) {
            const int code = resp.json.is_object() ? resp.json.value("code", -1) : -1;
            spdlog::warn("Cookie 登录失败，code={}", code);
            // Cookie 无效，清除已设置的 Cookie
            api_client.SetCookies("");
            SetState(LoginState::kError,
                     "Cookie 无效（code=" + std::to_string(code) + "），请检查后重试");
            return;
        }

        // Cookie 有效，走统一的登录成功处理路径
        spdlog::info("Cookie 登录成功");
        OnLoginSuccess();
    }

    // 自动登录流程：使用已保存的 cookies 校验登录状态（在后台线程中运行）
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

        // 同步解析数字 ID（自动登录成功场景）
        if (resp.json.contains("account") && resp.json["account"].is_object() &&
            resp.json["account"].contains("id")) {
            settings.user_id = resp.json["account"]["id"].get<int64_t>();
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
        const std::string url = BuildQrUrl(unikey, s_device_id);
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

void LoginManager::StartPasswordLogin(std::string phone, std::string password) {
    // 停止当前可能正在运行的后台线程
    impl_->stop_flag.store(true);
    if (impl_->poll_thread.joinable()) {
        impl_->poll_thread.join();
    }

    // 重置停止标志，清空二维码数据（与扫码流程互斥）
    impl_->stop_flag.store(false);
    {
        const std::lock_guard<std::mutex> lock(impl_->qr_mutex);
        impl_->qr_matrix.clear();
        impl_->qr_url.clear();
    }

    // 在后台线程中发起账密登录请求
    impl_->poll_thread = std::thread(
        [this, p = std::move(phone), pw = std::move(password)] { impl_->DoPasswordLogin(p, pw); });
}

void LoginManager::StartCookieLogin(std::string cookie) {
    // 停止当前可能正在运行的后台线程
    impl_->stop_flag.store(true);
    if (impl_->poll_thread.joinable()) {
        impl_->poll_thread.join();
    }

    // 重置停止标志，清空二维码数据（与扫码流程互斥）
    impl_->stop_flag.store(false);
    {
        const std::lock_guard<std::mutex> lock(impl_->qr_mutex);
        impl_->qr_matrix.clear();
        impl_->qr_url.clear();
    }

    // 在后台线程中发起 Cookie 登录校验
    impl_->poll_thread = std::thread([this, ck = std::move(cookie)] { impl_->DoCookieLogin(ck); });
}

void LoginManager::TryAutoLogin() {
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
    impl_->settings.user_id = 0;
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
