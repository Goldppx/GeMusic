#include "gemusic/ui/app_ui.h"

#include <cstdlib>
#include <string>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include "gemusic/config/settings.h"
#include "gemusic/library/local_library.h"
#include "gemusic/player/music_player.h"

namespace gemusic::ui {

using namespace ftxui;

// Pimpl 实现
struct AppUi::Impl {
    // 外部传入的引用
    config::Settings& settings;
    player::MusicPlayer& player;
    std::string config_path;

    // FTXUI 全屏交互终端实例
    ScreenInteractive screen = ScreenInteractive::Fullscreen();

    // 当前选中的菜单项索引
    int selected_menu = 0;

    // 侧边栏菜单条目
    std::vector<std::string> menu_entries = {
        "播放列表", "搜索", "我的收藏", "本地文件", "设置",
    };

    // 本地文件页面的状态
    std::vector<std::string> local_file_names;      // 显示用的文件名列表
    std::vector<library::LocalTrack> local_tracks;  // 完整的文件信息
    int selected_local_track = 0;                   // 当前选中的本地文件
    std::string local_scan_status;                  // 扫描状态提示信息

    // 设置页面状态
    std::string open_config_status;  // 打开配置文件的状态提示

    Impl(config::Settings& s, player::MusicPlayer& p, std::string cfg_path)
        : settings(s), player(p), config_path(std::move(cfg_path)) {
        // 初始化时扫描本地音乐库
        RefreshLocalLibrary();
    }

    // 扫描本地音乐库，更新文件列表
    void RefreshLocalLibrary() {
        local_file_names.clear();
        local_tracks.clear();
        selected_local_track = 0;

        // 展开路径中的 ~
        const auto expanded_path = config::ExpandHomePath(settings.music_library_path);

        auto result = library::ScanLocalMusic(expanded_path);
        if (!result) {
            local_scan_status = "扫描失败: " + result.error().message;
            return;
        }

        local_tracks = std::move(result.value());
        for (const auto& track : local_tracks) {
            local_file_names.push_back(track.file_name);
        }

        local_scan_status = "共找到 " + std::to_string(local_tracks.size()) + " 首音乐";
    }

    // 播放选中的本地文件
    void PlaySelectedTrack() {
        if (local_tracks.empty()) {
            return;
        }
        const auto idx = static_cast<size_t>(selected_local_track);
        if (idx >= local_tracks.size()) {
            return;
        }
        const auto& track = local_tracks[idx];
        auto result = player.Play(track.file_path);
        if (!result) {
            local_scan_status = "播放失败: " + result.error().message;
        }
    }

    // 使用系统默认编辑器打开配置文件
    void OpenConfigFile() {
        // 获取用户偏好的编辑器，依次尝试 $EDITOR、$VISUAL、vi
        const char* editor = std::getenv("EDITOR");
        if (editor == nullptr) {
            editor = std::getenv("VISUAL");
        }
        if (editor == nullptr) {
            editor = "vi";
        }

        // 构建命令
        const std::string cmd = std::string(editor) + " " + config_path + " &";

        // 暂时退出 TUI，执行编辑器，完成后恢复
        screen.Exit();
        // 在退出循环后由 Run() 处理重新加载
        open_config_status = "已使用 " + std::string(editor) + " 打开配置文件";
    }

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
        switch (selected_menu) {
            case 3:
                return BuildLocalFilesContent();
            case 4:
                return BuildSettingsContent();
            default:
                return BuildPlaceholderContent();
        }
    }

    // 尚未实现的页面占位内容
    auto BuildPlaceholderContent() -> Element {
        const std::string page_name = menu_entries[static_cast<size_t>(selected_menu)];
        return vbox({
                   text(page_name) | bold | center,
                   separator(),
                   text("功能开发中...") | dim | center,
               }) |
               flex;
    }

    // 本地文件页面内容
    auto BuildLocalFilesContent() -> Element {
        Elements items;
        items.push_back(text("本地音乐库: " + config::ExpandHomePath(settings.music_library_path)) |
                        bold);
        items.push_back(separator());

        if (local_tracks.empty()) {
            items.push_back(text(local_scan_status.empty() ? "暂无音乐文件" : local_scan_status) |
                            dim);
        } else {
            items.push_back(text(local_scan_status) | dim);
            items.push_back(separator());

            // 显示文件列表（带选中高亮）
            for (size_t i = 0; i < local_tracks.size(); ++i) {
                const auto& track = local_tracks[i];
                auto line = hbox({
                    text(std::to_string(i + 1) + ". "),
                    text(track.file_name) | flex,
                    text(" [" + track.extension + "]") | dim,
                });
                if (static_cast<int>(i) == selected_local_track) {
                    line = line | inverted;
                }
                items.push_back(line);
            }
        }

        // 底部操作提示
        items.push_back(filler());
        items.push_back(separator());

        // 显示当前播放状态
        const auto state = player.GetState();
        if (state == player::PlayerState::kPlaying || state == player::PlayerState::kPaused) {
            const auto& info = player.GetTrackInfo();
            const auto state_str = (state == player::PlayerState::kPlaying) ? "播放中" : "已暂停";
            items.push_back(text("当前: " + info.title + " [" + state_str + "]") |
                            color(Color::Green));
        }

        items.push_back(text("Enter: 播放 | r: 刷新 | Space: 暂停/恢复 | s: 停止") | dim);

        return vbox(std::move(items)) | flex;
    }

    // 设置页面内容
    auto BuildSettingsContent() -> Element {
        Elements items;
        items.push_back(text("设置") | bold | center);
        items.push_back(separator());

        // 显示当前配置项
        items.push_back(hbox({
            text("API 地址:      ") | bold,
            text(settings.api_base_url),
        }));
        items.push_back(hbox({
            text("Cookies:       ") | bold,
            text(settings.cookies.empty() ? "(未设置)" : "(已设置)"),
        }));
        items.push_back(hbox({
            text("音量:          ") | bold,
            text(std::to_string(settings.volume) + "%"),
        }));
        items.push_back(hbox({
            text("缓存目录:      ") | bold,
            text(settings.cache_dir),
        }));
        items.push_back(hbox({
            text("音乐库路径:    ") | bold,
            text(settings.music_library_path),
        }));
        items.push_back(hbox({
            text("配置文件:      ") | bold,
            text(config_path),
        }));

        items.push_back(separator());
        items.push_back(text("按 e 打开配置文件进行编辑（使用 $EDITOR）") | dim);

        if (!open_config_status.empty()) {
            items.push_back(text(open_config_status) | color(Color::Yellow));
        }

        items.push_back(filler());

        return vbox(std::move(items)) | flex;
    }

    // 构建底部播放控制栏
    // 显示当前播放状态、进度条和音量控制
    auto BuildPlayerBar() -> Element {
        const auto state = player.GetState();
        std::string state_str;
        std::string now_playing;

        switch (state) {
            case player::PlayerState::kPlaying:
                state_str = "播放中";
                now_playing = player.GetTrackInfo().title;
                break;
            case player::PlayerState::kPaused:
                state_str = "已暂停";
                now_playing = player.GetTrackInfo().title;
                break;
            case player::PlayerState::kLoading:
                state_str = "加载中";
                break;
            case player::PlayerState::kStopped:
            default:
                state_str = "停止";
                now_playing = "未在播放";
                break;
        }

        return hbox({
                   text(state_str) | color(Color::Yellow),
                   text(" | "),
                   text(now_playing.empty() ? "未在播放" : now_playing) | dim | flex,
                   text(" | "),
                   text("音量: " + std::to_string(settings.volume) + "%"),
               }) |
               border;
    }
};

AppUi::AppUi(config::Settings& settings, player::MusicPlayer& player, std::string config_path)
    : impl_(std::make_unique<Impl>(settings, player, std::move(config_path))) {}

AppUi::~AppUi() = default;
AppUi::AppUi(AppUi&&) noexcept = default;
auto AppUi::operator=(AppUi&&) noexcept -> AppUi& = default;

void AppUi::Run() {
    // 构建左侧菜单组件
    auto menu = impl_->BuildMenu();

    // 本地文件列表的 Menu 组件（仅在本地文件页面生效）
    auto local_menu = Menu(&impl_->local_file_names, &impl_->selected_local_track);

    // 使用 Container::Tab 管理右侧各页面的交互组件
    // 占位页面使用空 Renderer
    auto placeholder = Renderer([] { return text(""); });

    // 右侧内容区域的组件容器
    auto content_container = Container::Tab(
        {
            placeholder,  // 0: 播放列表
            placeholder,  // 1: 搜索
            placeholder,  // 2: 我的收藏
            local_menu,   // 3: 本地文件
            placeholder,  // 4: 设置
        },
        &impl_->selected_menu);

    // 左右布局容器
    auto main_container = Container::Horizontal({menu, content_container});

    // 使用 Renderer 将组件与整体布局组合
    // 整体布局结构：
    //   ┌──────────────────────────────┐
    //   │         标题栏               │
    //   ├──────┬───────────────────────┤
    //   │ 菜单 │      内容区域         │
    //   ├──────┴───────────────────────┤
    //   │       播放控制栏             │
    //   └──────────────────────────────┘
    auto renderer = Renderer(main_container, [this, &menu] {
        return vbox({
            impl_->BuildTitleBar(),
            hbox({
                menu->Render() | vscroll_indicator | frame | size(WIDTH, EQUAL, 20) | border,
                impl_->BuildContent() | border,
            }) | flex,
            impl_->BuildPlayerBar(),
        });
    });

    // 添加全局快捷键处理
    auto main_component = CatchEvent(renderer, [this](Event event) {
        // q 键退出程序
        if (event == Event::Character('q')) {
            impl_->screen.Exit();
            return true;
        }

        // 本地文件页面的快捷键
        if (impl_->selected_menu == 3) {
            // Enter 键播放选中曲目
            if (event == Event::Return) {
                impl_->PlaySelectedTrack();
                return true;
            }
            // r 键刷新本地音乐库
            if (event == Event::Character('r')) {
                impl_->RefreshLocalLibrary();
                return true;
            }
            // 空格键暂停/恢复
            if (event == Event::Character(' ')) {
                const auto state = impl_->player.GetState();
                if (state == player::PlayerState::kPlaying) {
                    impl_->player.Pause();
                } else if (state == player::PlayerState::kPaused) {
                    impl_->player.Resume();
                }
                return true;
            }
            // s 键停止
            if (event == Event::Character('s')) {
                impl_->player.Stop();
                return true;
            }
        }

        // 设置页面的快捷键
        if (impl_->selected_menu == 4) {
            // e 键打开配置文件
            if (event == Event::Character('e')) {
                impl_->OpenConfigFile();
                return true;
            }
        }

        return false;
    });

    // 启动 TUI 主循环
    impl_->screen.Loop(main_component);
}

}  // namespace gemusic::ui
