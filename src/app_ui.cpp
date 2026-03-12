#include "gemusic/ui/app_ui.h"

#include <string>

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

namespace gemusic::ui {

using namespace ftxui;

// Pimpl 实现
struct AppUi::Impl {
    // FTXUI 全屏交互终端实例
    ScreenInteractive screen = ScreenInteractive::Fullscreen();

    // 当前选中的菜单项索引
    int selected_menu = 0;

    // 侧边栏菜单条目
    std::vector<std::string> menu_entries = {
        "播放列表",
        "搜索",
        "我的收藏",
        "本地文件",
        "设置",
    };

    // 构建顶部标题栏
    // 返回一个带有应用名称和装饰边框的 Element
    auto BuildTitleBar() -> Element {
        return hbox({
                   text("GeMusic") | bold | color(Color::Cyan),
                   text(" - 终端音乐播放器") | dim,
               }) |
               border;
    }

    // 构建左侧菜单面板
    // 使用 FTXUI Menu 组件，允许通过上下键选择不同的功能页面
    auto BuildMenu() -> Component {
        return Menu(&menu_entries, &selected_menu);
    }

    // 构建右侧内容区域
    // 根据当前选中的菜单项显示对应的页面内容
    auto BuildContent() -> Element {
        // 根据菜单索引显示不同内容（后续逐步实现）
        std::string page_name = menu_entries[static_cast<size_t>(selected_menu)];
        return vbox({
                   text(page_name) | bold | center,
                   separator(),
                   text("功能开发中...") | dim | center,
               }) |
               flex;
    }

    // 构建底部播放控制栏
    // 显示当前播放状态、进度条和音量控制
    auto BuildPlayerBar() -> Element {
        return hbox({
                   text("停止") | color(Color::Yellow),
                   text(" | "),
                   text("未在播放") | dim | flex,
                   text(" | "),
                   text("音量: 80%"),
               }) |
               border;
    }
};

AppUi::AppUi() : impl_(std::make_unique<Impl>()) {}

AppUi::~AppUi() = default;
AppUi::AppUi(AppUi&&) noexcept = default;
auto AppUi::operator=(AppUi&&) noexcept -> AppUi& = default;

void AppUi::Run() {
    // 构建左侧菜单组件
    auto menu = impl_->BuildMenu();

    // 使用 Renderer 将菜单组件与整体布局组合
    // 整体布局结构：
    //   ┌──────────────────────────────┐
    //   │         标题栏               │
    //   ├──────┬───────────────────────┤
    //   │ 菜单 │      内容区域         │
    //   ├──────┴───────────────────────┤
    //   │       播放控制栏             │
    //   └──────────────────────────────┘
    auto renderer = Renderer(menu, [this, &menu] {
        return vbox({
            impl_->BuildTitleBar(),
            hbox({
                menu->Render() | vscroll_indicator | frame |
                    size(WIDTH, EQUAL, 20) | border,
                impl_->BuildContent() | border,
            }) | flex,
            impl_->BuildPlayerBar(),
        });
    });

    // 添加全局快捷键处理
    // q 键退出程序
    auto main_component = CatchEvent(renderer, [this](Event event) {
        if (event == Event::Character('q')) {
            impl_->screen.Exit();
            return true;
        }
        return false;
    });

    // 启动 TUI 主循环
    impl_->screen.Loop(main_component);
}

}  // namespace gemusic::ui
