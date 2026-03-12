#ifndef GEMUSIC_UI_APP_UI_H
#define GEMUSIC_UI_APP_UI_H

#include <memory>
#include <string>

#include "gemusic/auth/login_manager.h"
#include "gemusic/config/settings.h"
#include "gemusic/player/music_player.h"

namespace gemusic::ui {

// 应用主界面类
// 负责构建整个 TUI 布局，包含：
// - 顶部标题栏
// - 左侧播放列表面板
// - 右侧播放控制面板
// - 底部状态栏（播放进度、音量等）
class AppUi {
   public:
    // 构造时传入配置、播放器、登录管理器引用和配置文件路径
    AppUi(config::Settings& settings, player::MusicPlayer& player,
          auth::LoginManager& login_manager, std::string config_path);
    ~AppUi();

    // 禁止拷贝，允许移动
    AppUi(const AppUi&) = delete;
    auto operator=(const AppUi&) -> AppUi& = delete;
    AppUi(AppUi&&) noexcept;
    auto operator=(AppUi&&) noexcept -> AppUi&;

    // 启动 TUI 主循环（阻塞调用）
    void Run();

   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace gemusic::ui

#endif  // GEMUSIC_UI_APP_UI_H
