#ifndef GEMUSIC_AUTH_LOGIN_MANAGER_H
#define GEMUSIC_AUTH_LOGIN_MANAGER_H

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "gemusic/config/settings.h"

namespace gemusic::auth {

// 二维码登录状态枚举
// 启动自动登录路径：kVerifying → kLoggedIn
//                             → kIdle（cookies 无效或网络错误）
// 手动扫码路径：    kIdle → kFetchingKey → kWaitingScan → kWaitingConfirm → kLoggedIn
//                                                                          → kExpired
enum class LoginState {
    kIdle,            // 初始状态，尚未开始登录流程
    kVerifying,       // 正在使用已保存的 cookies 校验登录状态
    kFetchingKey,     // 正在向服务器请求二维码唯一 key
    kWaitingScan,     // 二维码已生成，等待用户扫描
    kWaitingConfirm,  // 用户已扫描，等待在手机上确认
    kLoggedIn,        // 登录成功
    kExpired,         // 二维码已过期（需重新开始）
    kError,           // 网络或解析错误
};

// 登录管理器
// 负责网易云音乐二维码登录全流程：
//   1. 请求 unikey（/weapi/login/qrcode/unikey）
//   2. 构造二维码 URL 并用 qrcodegen 生成 QR 码矩阵
//   3. 后台线程轮询登录状态（/weapi/login/qrcode/client/login）
//   4. 登录成功后提取 MUSIC_U cookie，写入 Settings 并保存到磁盘
//
// 线程安全：GetState()、GetQrMatrix()、GetUserName() 可在任意线程调用
class LoginManager {
   public:
    // 参数: settings       - 应用配置引用（登录成功后写入 cookies 和 user_name）
    //        config_path   - 配置文件路径（用于保存 cookies 到磁盘）
    //        on_state_change - 状态变化回调（在后台线程调用，UI 需通过 PostEvent 刷新）
    LoginManager(config::Settings& settings, std::string config_path,
                 std::function<void()> on_state_change);
    ~LoginManager();

    // 禁止拷贝，允许移动
    LoginManager(const LoginManager&) = delete;
    auto operator=(const LoginManager&) -> LoginManager& = delete;
    LoginManager(LoginManager&&) noexcept;
    auto operator=(LoginManager&&) noexcept -> LoginManager&;

    // 开始二维码登录流程（异步，立即返回）
    // 内部会先获取 unikey，生成二维码，然后启动后台轮询线程
    void StartLogin();

    // 自动登录：若 settings.cookies 非空，则异步校验 cookies 是否仍有效
    // - 有效  → 状态切换至 kLoggedIn，填充 user_name
    // - 无效  → 清除 cookies 和 user_name，保存配置，切换至 kIdle
    // - 网络错误 → 保留旧 cookies，切换至 kIdle（让用户决定是否重试）
    // - cookies 为空 → 直接返回，不做任何操作
    // 若后台线程正在运行（如前一次校验未完成），会先中断旧线程再启动新线程
    // 与 StartLogin() 互斥：调用 StartLogin() 会中断正在进行的自动登录校验
    void TryAutoLogin();

    // 退出登录：清除 cookies、user_name，保存配置，重置状态为 kIdle
    void Logout();

    // 替换状态变化回调（在 AppUi::Run() 启动后调用，注册屏幕刷新函数）
    void SetOnStateChange(std::function<void()> callback);

    // 获取当前登录状态（线程安全）
    [[nodiscard]] auto GetState() const -> LoginState;

    // 获取二维码矩阵的拷贝（只在 kWaitingScan / kWaitingConfirm 状态下有效）
    // 返回: 每行是一个 bool 向量，true 表示深色模块；空矩阵表示二维码未就绪
    // 注意: 返回拷贝以避免跨线程的数据竞争
    [[nodiscard]] auto GetQrMatrix() const -> std::vector<std::vector<bool>>;

    // 获取当前状态的描述文字（供 UI 显示）
    [[nodiscard]] auto GetStatusText() const -> std::string;

    // 获取二维码的目标 URL（供调试或外部显示）
    [[nodiscard]] auto GetQrUrl() const -> std::string;

   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace gemusic::auth

#endif  // GEMUSIC_AUTH_LOGIN_MANAGER_H
