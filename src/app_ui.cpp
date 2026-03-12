#include "gemusic/ui/app_ui.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include "gemusic/auth/login_manager.h"
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
    auth::LoginManager& login_manager;
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

    // 设置页面登录区的三个内联按钮（在 Run() 中初始化）
    // 渲染为 "[label]" 样式的可聚焦文字链接
    Component btn_scan_login;  // [扫码登录] — 触发 StartLogin()
    Component btn_refresh;     // [刷新]     — 触发 TryAutoLogin()
    Component btn_logout;      // [退出登录] — 触发 Logout()

    Impl(config::Settings& s, player::MusicPlayer& p, auth::LoginManager& lm, std::string cfg_path)
        : settings(s), player(p), login_manager(lm), config_path(std::move(cfg_path)) {
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
    // 未播放时显示 "GeMusic - 终端音乐播放器"
    // 播放/暂停时显示 "GeMusic  ♪ 歌名 - 艺术家  ▶/⏸"
    auto BuildTitleBar() -> Element {
        const auto state = player.GetState();
        const bool has_track =
            (state == player::PlayerState::kPlaying || state == player::PlayerState::kPaused);

        if (!has_track) {
            return hbox({
                       text("GeMusic") | bold | color(Color::Cyan),
                       text(" - 终端音乐播放器") | dim,
                   }) |
                   border;
        }

        const auto& info = player.GetTrackInfo();
        const auto state_icon = (state == player::PlayerState::kPlaying) ? "▶ " : "⏸ ";

        Elements items;
        items.push_back(text("GeMusic") | bold | color(Color::Cyan));
        items.push_back(text("  ♪ "));
        items.push_back(text(info.title.empty() ? "未知曲目" : info.title) | color(Color::Green));
        if (!info.artist.empty()) {
            items.push_back(text(" - " + info.artist) | dim);
        }
        items.push_back(text("  "));
        items.push_back(text(state_icon) | color(Color::Yellow));

        return hbox(std::move(items)) | border;
    }

    // 构建左侧菜单面板
    // 使用 FTXUI Menu 组件，允许通过上下键选择不同的功能页面
    auto BuildMenu() -> Component {
        return Menu(&menu_entries, &selected_menu);
    }

    // 构建右侧内容区域
    // 根据当前选中的菜单项显示对应的页面内容
    // 注意：此方法仅供占位页面使用，交互页面（本地文件、设置）
    //       通过 Renderer(component, fn) 包装，在 Run() 中直接渲染
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
        items.push_back(text("Enter: 播放 | r: 刷新 | Space: 暂停/恢复 | s: 停止") | dim);

        return vbox(std::move(items)) | flex;
    }

    // 将二维码矩阵渲染为半块字符 Element，纵向合并两行为一行
    // 利用 Unicode 半块字符将高度压缩为原来的一半：
    //   上深下深 → "█" (U+2588，全块)
    //   上深下浅 → "▀" (U+2580，上半块)
    //   上浅下深 → "▄" (U+2584，下半块)
    //   上浅下浅 → " "（空格）
    // 横向每个模块仍占一个字符宽度（不再双倍）
    // 四周添加 2 格安静区（模块数），符合 QR 码最低要求
    static auto BuildQrAscii(const std::vector<std::vector<bool>>& matrix) -> Element {
        if (matrix.empty()) {
            return text("二维码生成中...") | dim | center;
        }

        constexpr int kQuietZone = 2;  // 安静区宽度（模块数，缩减为 2）
        const auto size = static_cast<int>(matrix.size());
        const int total_cols = size + 2 * kQuietZone;
        // 加上安静区后的总行数
        const int total_rows = size + 2 * kQuietZone;

        // 构造含安静区的扁平布尔矩阵（true = 深色）
        // padded[r][c] = false 表示安静区（浅色白边）
        std::vector<std::vector<bool>> padded(
            static_cast<size_t>(total_rows),
            std::vector<bool>(static_cast<size_t>(total_cols), false));
        for (int r = 0; r < size; ++r) {
            for (int c = 0; c < size; ++c) {
                padded[static_cast<size_t>(r + kQuietZone)][static_cast<size_t>(c + kQuietZone)] =
                    matrix[static_cast<size_t>(r)][static_cast<size_t>(c)];
            }
        }

        // 以步长 2 遍历行，将相邻两行合并为一行半块字符
        Elements rows;
        // 若总行数为奇数，最后一行以"上深下浅"或"上浅下浅"处理（补一空行）
        for (int r = 0; r < total_rows; r += 2) {
            std::string line;
            for (int c = 0; c < total_cols; ++c) {
                const bool top = padded[static_cast<size_t>(r)][static_cast<size_t>(c)];
                // 若 r+1 超出范围（奇数总行时最后一行），视下半为浅色
                const bool bot = (r + 1 < total_rows)
                                     ? padded[static_cast<size_t>(r + 1)][static_cast<size_t>(c)]
                                     : false;
                if (top && bot) {
                    line += "█";  // U+2588 全块
                } else if (top) {
                    line += "▀";  // U+2580 上半块
                } else if (bot) {
                    line += "▄";  // U+2584 下半块
                } else {
                    line += " ";  // 空格
                }
            }
            rows.push_back(text(line));
        }

        return vbox(std::move(rows)) | center;
    }

    // 创建内联样式的按钮（渲染为 "[label]" 形式的文字链接）
    // 聚焦时加粗并染青色，以区分当前焦点位置
    static auto MakeInlineButton(std::string label, std::function<void()> on_click) -> Component {
        ButtonOption opt;
        opt.transform = [](const EntryState& s) -> Element {
            auto elem = text("[" + s.label + "]");
            if (s.focused) {
                return elem | bold | color(Color::Cyan);
            }
            return elem | color(Color::Blue);
        };
        return Button(std::move(label), std::move(on_click), std::move(opt));
    }

    // 构建设置页面顶部的登录区域
    // 根据 LoginManager 的当前状态显示不同内容，按钮以内联 "[label]" 形式嵌入文字行：
    //   - kLoggedIn         : ● 已登录：用户名 [刷新] [退出登录]
    //   - kWaitingScan/Confirm: QR 二维码 + 状态文字 + [重新扫码]
    //   - kVerifying        : 正在验证... [刷新] [扫码登录]
    //   - kFetchingKey      : 正在连接...（无按钮）
    //   - kError            : ✗ 错误信息 [重试] [刷新]
    //   - kExpired          : ⏱ 已过期 [重新扫码] [刷新]
    //   - kIdle             : 未登录 [扫码登录]
    auto BuildLoginSection() -> Element {
        const auto login_state = login_manager.GetState();
        const auto status_text = login_manager.GetStatusText();

        // 按钮未初始化时（Run() 调用前）退化为纯文本显示
        const bool btns_ready = static_cast<bool>(btn_scan_login);

        Elements items;

        switch (login_state) {
            case auth::LoginState::kLoggedIn: {
                // 已登录：用户名与操作按钮同行
                Elements row;
                row.push_back(text("● ") | color(Color::Green));
                row.push_back(text("已登录：") | bold);
                row.push_back(text(settings.user_name.empty() ? "(未知用户)" : settings.user_name) |
                              color(Color::Cyan));
                if (btns_ready) {
                    row.push_back(text("  "));
                    row.push_back(btn_refresh->Render());
                    row.push_back(text("  "));
                    row.push_back(btn_logout->Render());
                }
                items.push_back(hbox(std::move(row)) | center);
                break;
            }

            case auth::LoginState::kWaitingScan:
            case auth::LoginState::kWaitingConfirm: {
                // 等待扫描/确认：显示二维码与重扫按钮
                const auto qr_matrix = login_manager.GetQrMatrix();
                items.push_back(BuildQrAscii(qr_matrix));
                items.push_back(text(status_text.empty() ? "请使用网易云 App 扫描" : status_text) |
                                color(Color::Yellow) | center);
                if (btns_ready) {
                    items.push_back(hbox({text("  "), btn_scan_login->Render()}) | center);
                }
                break;
            }

            case auth::LoginState::kVerifying: {
                // 校验已保存的 cookies，允许跳过直接扫码
                Elements row;
                row.push_back(text("正在验证登录状态...") | dim);
                if (btns_ready) {
                    row.push_back(text("  "));
                    row.push_back(btn_refresh->Render());
                    row.push_back(text("  "));
                    row.push_back(btn_scan_login->Render());
                }
                items.push_back(hbox(std::move(row)) | center);
                break;
            }

            case auth::LoginState::kFetchingKey:
                items.push_back(text("正在连接服务器...") | dim | center);
                break;

            case auth::LoginState::kError: {
                Elements row;
                row.push_back(text("✗ ") | color(Color::Red));
                row.push_back(text(status_text.empty() ? "登录出错" : status_text) |
                              color(Color::Red));
                if (btns_ready) {
                    row.push_back(text("  "));
                    row.push_back(btn_scan_login->Render());
                    row.push_back(text("  "));
                    row.push_back(btn_refresh->Render());
                }
                items.push_back(hbox(std::move(row)) | center);
                break;
            }

            case auth::LoginState::kExpired: {
                Elements row;
                row.push_back(text("⏱ ") | color(Color::Yellow));
                row.push_back(text("二维码已过期") | color(Color::Yellow));
                if (btns_ready) {
                    row.push_back(text("  "));
                    row.push_back(btn_scan_login->Render());
                    row.push_back(text("  "));
                    row.push_back(btn_refresh->Render());
                }
                items.push_back(hbox(std::move(row)) | center);
                break;
            }

            case auth::LoginState::kIdle:
            default: {
                Elements row;
                row.push_back(text("未登录") | dim);
                if (btns_ready) {
                    row.push_back(text("  "));
                    row.push_back(btn_scan_login->Render());
                }
                items.push_back(hbox(std::move(row)) | center);
                break;
            }
        }

        return vbox(std::move(items)) | border;
    }

    // 设置页面内容（登录区在最顶部）
    auto BuildSettingsContent() -> Element {
        Elements items;
        items.push_back(text("设置") | bold | center);
        items.push_back(separator());

        // ── 登录区域（最上方）──
        items.push_back(BuildLoginSection());
        items.push_back(separator());

        // ── 常规配置项 ──
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
        items.push_back(text("e: 用编辑器打开配置文件") | dim);

        if (!open_config_status.empty()) {
            items.push_back(text(open_config_status) | color(Color::Yellow));
        }

        items.push_back(filler());

        return vbox(std::move(items)) | flex;
    }

    // 将毫秒数格式化为 "M:SS" 字符串（分钟不补零，秒数补零）
    static auto FormatTime(uint32_t ms) -> std::string {
        const uint32_t total_sec = ms / 1000;
        const uint32_t minutes = total_sec / 60;
        const uint32_t seconds = total_sec % 60;
        const std::string sec_str = (seconds < 10 ? "0" : "") + std::to_string(seconds);
        return std::to_string(minutes) + ":" + sec_str;
    }

    // 构建底部播放控制栏
    // 播放/暂停时：状态图标 | 已播放时间 ████░░ 总时长 | 音量
    // 其他状态：状态文字 + 音量
    auto BuildPlayerBar() -> Element {
        const auto state = player.GetState();
        const auto vol_text = "音量: " + std::to_string(settings.volume) + "%";

        if (state == player::PlayerState::kPlaying || state == player::PlayerState::kPaused) {
            const auto& info = player.GetTrackInfo();
            const uint32_t pos = info.position_ms;
            const uint32_t dur = info.duration_ms;
            // 计算进度比例（防止除零；dur 为 0 时显示空进度条）
            const float ratio =
                (dur > 0) ? static_cast<float>(pos) / static_cast<float>(dur) : 0.0F;
            const auto state_icon = (state == player::PlayerState::kPlaying) ? "▶ " : "⏸ ";

            return hbox({
                       text(state_icon) | color(Color::Yellow),
                       text(FormatTime(pos)) | dim,
                       text(" "),
                       gauge(ratio) | flex | color(Color::Cyan),
                       text(" "),
                       text(FormatTime(dur)) | dim,
                       text("  "),
                       text(vol_text) | dim,
                   }) |
                   border;
        }

        if (state == player::PlayerState::kLoading) {
            return hbox({text("⏳ 加载中") | dim | flex, text(vol_text) | dim}) | border;
        }

        // kStopped
        return hbox({text("■ 停止") | dim | flex, text(vol_text) | dim}) | border;
    }
};

AppUi::AppUi(config::Settings& settings, player::MusicPlayer& player,
             auth::LoginManager& login_manager, std::string config_path)
    : impl_(std::make_unique<Impl>(settings, player, login_manager, std::move(config_path))) {}

AppUi::~AppUi() = default;
AppUi::AppUi(AppUi&&) noexcept = default;
auto AppUi::operator=(AppUi&&) noexcept -> AppUi& = default;

void AppUi::Run() {
    // 注册登录状态变化时触发屏幕刷新的回调
    // 后台轮询线程每次更新状态时，通过 PostEvent(Custom) 唤醒 FTXUI 事件循环重绘界面
    impl_->login_manager.SetOnStateChange(
        [this] { impl_->screen.PostEvent(ftxui::Event::Custom); });

    // 若配置文件中已有 cookies，启动异步校验（自动登录）
    // 回调已注册，状态变化时 UI 可正常刷新
    impl_->login_manager.TryAutoLogin();
    // ── 初始化设置页面的登录区内联按钮 ──
    // 按钮渲染为 "[label]" 文字链接，Tab 键可在三者间切换焦点，Enter 触发回调
    impl_->btn_scan_login =
        Impl::MakeInlineButton("扫码登录", [this] { impl_->login_manager.StartLogin(); });
    impl_->btn_refresh =
        Impl::MakeInlineButton("刷新", [this] { impl_->login_manager.TryAutoLogin(); });
    impl_->btn_logout =
        Impl::MakeInlineButton("退出登录", [this] { impl_->login_manager.Logout(); });

    // 三个按钮放入 Horizontal 容器，Tab 键可依次聚焦
    auto settings_buttons =
        Container::Horizontal({impl_->btn_scan_login, impl_->btn_refresh, impl_->btn_logout});
    // 构建左侧菜单组件
    auto menu = impl_->BuildMenu();

    // 本地文件列表的 Menu 组件（处理上下键导航，同步 selected_local_track）
    auto local_menu = Menu(&impl_->local_file_names, &impl_->selected_local_track);

    // 使用 Renderer(component, fn) 为每个页面包装组件树与渲染树：
    //   - component：负责焦点管理与事件处理（键盘导航、按钮激活等）
    //   - fn：负责渲染输出，可读取 component 管理的状态（如选中索引）
    // 这样确保组件树（事件/焦点）与渲染树（Element）保持对齐，
    // 解决直接调用 BuildXxx() 导致的渲染链断裂、按钮无法获取焦点的问题。

    // 占位页面：各自独立实例，避免同一 Component 被 Add 到同一容器多次
    auto placeholder_0 = Renderer([this] { return impl_->BuildPlaceholderContent(); });
    auto placeholder_1 = Renderer([this] { return impl_->BuildPlaceholderContent(); });
    auto placeholder_2 = Renderer([this] { return impl_->BuildPlaceholderContent(); });

    // 本地文件页面：local_menu 负责上下键导航（维护 selected_local_track），
    // fn 提供自定义文件列表渲染（高亮、状态提示等）
    auto local_files_page =
        Renderer(local_menu, [this] { return impl_->BuildLocalFilesContent(); });

    // 设置页面：settings_buttons 负责 Tab/Enter 焦点切换与按钮触发，
    // fn 提供完整设置页面渲染（包含登录区内联按钮）
    auto settings_page =
        Renderer(settings_buttons, [this] { return impl_->BuildSettingsContent(); });

    // 右侧内容区域的组件容器（Tab 页由 selected_menu 索引控制）
    auto content_container = Container::Tab(
        {
            placeholder_0,     // 0: 播放列表
            placeholder_1,     // 1: 搜索
            placeholder_2,     // 2: 我的收藏
            local_files_page,  // 3: 本地文件（带 local_menu 交互）
            settings_page,     // 4: 设置（带 settings_buttons 交互）
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
    // 调用 content_container->Render() 而非 BuildContent()，
    // 确保渲染链从根组件发起，焦点状态能正确传递至各子组件
    auto renderer = Renderer(main_container, [this, &menu, &content_container] {
        return vbox({
            impl_->BuildTitleBar(),
            hbox({
                menu->Render() | vscroll_indicator | frame | size(WIDTH, EQUAL, 20) | border,
                content_container->Render() | flex | border,
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

    // 启动 500ms 定时刷新线程，驱动播放进度条更新
    // 线程通过 PostEvent(Custom) 触发 FTXUI 重绘，Loop 返回后通过 stop_refresh 标志退出
    std::atomic<bool> stop_refresh{false};
    std::thread refresh_thread([&] {
        while (!stop_refresh.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            if (!stop_refresh.load()) {
                impl_->screen.PostEvent(ftxui::Event::Custom);
            }
        }
    });

    // 启动 TUI 主循环（阻塞直到用户退出）
    impl_->screen.Loop(main_component);

    // 主循环退出后，通知刷新线程停止并等待其结束
    stop_refresh.store(true);
    refresh_thread.join();
}

}  // namespace gemusic::ui
