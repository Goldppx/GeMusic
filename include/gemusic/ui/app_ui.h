#ifndef GEMUSIC_UI_APP_UI_H
#define GEMUSIC_UI_APP_UI_H

#include <memory>

namespace gemusic::ui {

// 应用主界面类
// 负责构建整个 TUI 布局，包含：
// - 顶部标题栏
// - 左侧播放列表面板
// - 右侧播放控制面板
// - 底部状态栏（播放进度、音量等）
class AppUi {
public:
    AppUi();
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
