#include "gemusic/ui/app_ui.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include "gemusic/auth/login_manager.h"
#include "gemusic/config/settings.h"
#include "gemusic/library/local_library.h"
#include "gemusic/network/api_client.h"
#include "gemusic/network/netease_api.h"
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
        "播放列表", "搜索", "我的歌单", "本地文件", "设置",
    };

    // 本地文件页面的状态
    std::vector<std::string> local_file_names;      // 显示用的文件名列表
    std::vector<library::LocalTrack> local_tracks;  // 完整的文件信息
    int selected_local_track = 0;                   // 当前选中的本地文件
    std::string local_scan_status;                  // 扫描状态提示信息

    // 设置页面状态
    std::string open_config_status;  // 打开配置文件的状态提示

    // ── 我的歌单页面状态 ──
    // 受 playlists_mutex_ 保护，后台线程写入，UI 线程只读
    std::vector<network::Playlist> user_playlists_;
    // 供 FTXUI Menu 组件使用的显示名称列表（格式："[自] 歌单名 (N首)" 或 "歌单名 (N首)"）
    std::vector<std::string> playlist_display_names_;
    // 当前在歌单列表中选中的条目索引
    int selected_playlist_ = 0;
    // 状态提示文字（"加载中..." / "共 N 个歌单" / 错误信息）
    std::string playlist_status_;
    // true 表示后台请求正在进行中
    bool playlists_loading_ = false;
    // 保护歌单数据的互斥锁
    mutable std::mutex playlists_mutex_;
    // 用于 FetchUserPlaylists 的 HTTP 客户端（专用实例，含已登录 cookies）
    network::ApiClient playlists_api_client_{"", ""};

    // ── 歌单详情视图状态 ──
    // 0 = 歌单列表视图，1 = 曲目列表视图；同时作为 Container::Tab 索引
    int playlist_view_index_ = 0;
    // 当前打开的歌单 ID（用于防止重复加载）
    int64_t open_playlist_id_ = 0;
    // 当前打开的歌单名称（用于详情页标题）
    std::string open_playlist_name_;
    // 当前打开歌单的完整曲目列表（受 tracks_mutex_ 保护）
    std::vector<network::Track> open_playlist_tracks_;
    // 供 FTXUI Menu 组件使用的曲目显示名称列表（"序号. 歌名 - 艺术家"）
    std::vector<std::string> track_display_names_;
    // 当前在曲目列表中选中的条目索引
    int selected_track_ = 0;
    // 曲目列表状态提示（"加载中..." / "共 N 首" / 错误信息）
    std::string track_list_status_;
    // true 表示曲目列表后台请求进行中
    bool track_list_loading_ = false;
    // 保护曲目数据的互斥锁（加锁顺序：先 tracks_mutex_ 后 queue_mutex_）
    mutable std::mutex tracks_mutex_;

    // ── 播放队列状态 ──
    // 播放队列条目（歌曲 ID + 显示名称 + 可选本地路径）
    struct QueueEntry {
        int64_t song_id;
        std::string display_name;
        std::string local_path;  // 非空时直接播放本地文件，跳过网络请求
    };
    // 当前播放队列
    std::vector<QueueEntry> play_queue_;
    // 当前播放队列中正在播放的索引（-1 表示无）
    int current_queue_index_ = -1;
    // 显示在播放控制栏的队列状态（如 "♪ 歌名 (3/15)"）
    std::string queue_status_;
    // 保护队列数据的互斥锁（加锁顺序：先 tracks_mutex_ 后 queue_mutex_）
    mutable std::mutex queue_mutex_;
    // 播放列表页面中当前选中的条目索引（供 FTXUI Menu 组件与键盘导航使用）
    int selected_queue_track_ = 0;
    // 供 FTXUI Menu 组件使用的队列条目显示名称列表（与 play_queue_ 保持同步）
    std::vector<std::string> queue_display_names_;

    // 设置页面登录区的三个内联按钮（在 Run() 中初始化）
    // 渲染为 "[label]" 样式的可聚焦文字链接
    Component btn_scan_login;  // [扫码登录] — 触发 StartLogin()
    Component btn_refresh;     // [刷新]     — 触发 TryAutoLogin()
    Component btn_logout;      // [退出登录] — 触发 Logout()

    // ── 账密登录表单状态 ──
    // 0 = 正常按钮行，1 = 账密登录表单（Container::Tab 切换索引）
    int password_form_tab_ = 0;
    // 手机号输入框绑定值
    std::string phone_input_value_;
    // 密码输入框绑定值（掩码显示）
    std::string password_input_value_;

    Component btn_password_login;  // [账密登录] — 切换至账密登录表单
    Component input_phone;         // 手机号输入框
    Component input_password;      // 密码输入框（掩码）
    Component btn_confirm_login;   // [确认登录]
    Component btn_cancel_form;     // [取消]

    // ── Cookie 登录表单状态 ──
    // password_form_tab_ == 2 时显示 Cookie 登录表单
    std::string cookie_input_value_;     // Cookie 输入框绑定值
    Component btn_cookie_login;          // [Cookie登录] — 切换至 Cookie 登录表单
    Component input_cookie;              // Cookie 输入框
    Component btn_confirm_cookie_login;  // [确认登录]（Cookie 表单）
    Component btn_cancel_cookie_form;    // [取消]（Cookie 表单）

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

    // 在后台线程中拉取用户歌单列表
    // 完成后通过 screen.PostEvent(Custom) 触发 UI 刷新
    // 参数: screen_ref - FTXUI 屏幕引用（用于 PostEvent）
    void LoadUserPlaylists(ScreenInteractive& screen_ref) {
        {
            const std::lock_guard<std::mutex> lock(playlists_mutex_);
            // 避免重复请求
            if (playlists_loading_) {
                return;
            }
            playlists_loading_ = true;
            playlist_status_ = "加载中...";
        }
        // 刷新一次让"加载中..."立即显示
        screen_ref.PostEvent(ftxui::Event::Custom);

        // 在独立线程中发起网络请求，避免阻塞渲染线程
        std::thread([this, &screen_ref] {
            // 将当前已登录的 cookies 同步到专用客户端
            playlists_api_client_.SetCookies(settings.cookies);

            auto result = network::FetchUserPlaylists(playlists_api_client_, settings.user_id);

            {
                const std::lock_guard<std::mutex> lock(playlists_mutex_);
                playlists_loading_ = false;

                if (!result) {
                    // 请求失败，显示错误信息
                    playlist_status_ = "获取失败: " + result.error().message;
                    user_playlists_.clear();
                    playlist_display_names_.clear();
                } else {
                    user_playlists_ = std::move(result.value());
                    // 构造显示名称："名称 (N首)"（不区分自建或收藏歌单）
                    playlist_display_names_.clear();
                    playlist_display_names_.reserve(user_playlists_.size());
                    for (const auto& pl : user_playlists_) {
                        std::string display = pl.name;
                        display += " (" + std::to_string(pl.track_count) + "首)";
                        playlist_display_names_.push_back(std::move(display));
                    }
                    playlist_status_ = "共 " + std::to_string(user_playlists_.size()) + " 个歌单";
                    selected_playlist_ = 0;
                }
            }
            // 通知 UI 刷新
            screen_ref.PostEvent(ftxui::Event::Custom);
        }).detach();
    }

    // ── 歌单详情与播放队列相关方法 ──

    // 打开选中歌单的曲目详情视图；若 ID 未变则使用缓存直接切换
    void OpenPlaylistDetail(ScreenInteractive& screen_ref) {
        int64_t playlist_id = 0;
        std::string playlist_name;
        {
            const std::lock_guard<std::mutex> lock(playlists_mutex_);
            if (user_playlists_.empty())
                return;
            const auto idx = static_cast<size_t>(selected_playlist_);
            if (idx >= user_playlists_.size())
                return;
            playlist_id = user_playlists_[idx].id;
            playlist_name = user_playlists_[idx].name;
        }
        // 切换到曲目视图
        playlist_view_index_ = 1;
        open_playlist_name_ = playlist_name;
        // 若已缓存同一歌单，直接展示
        {
            const std::lock_guard<std::mutex> lock(tracks_mutex_);
            if (playlist_id == open_playlist_id_ && !open_playlist_tracks_.empty()) {
                screen_ref.PostEvent(ftxui::Event::Custom);
                return;
            }
            open_playlist_id_ = playlist_id;
            open_playlist_tracks_.clear();
            track_display_names_.clear();
            selected_track_ = 0;
            track_list_loading_ = true;
            track_list_status_ = "加载中...";
        }
        screen_ref.PostEvent(ftxui::Event::Custom);
        // 后台线程请求曲目列表
        std::thread([this, playlist_id, &screen_ref] {
            network::ApiClient api_client{"", settings.cookies};
            auto result = network::FetchPlaylistTracks(api_client, playlist_id);
            {
                const std::lock_guard<std::mutex> lock(tracks_mutex_);
                track_list_loading_ = false;
                if (!result) {
                    track_list_status_ = "获取失败: " + result.error().message;
                    open_playlist_tracks_.clear();
                    track_display_names_.clear();
                } else {
                    open_playlist_tracks_ = std::move(result.value());
                    track_display_names_.clear();
                    track_display_names_.reserve(open_playlist_tracks_.size());
                    for (size_t i = 0; i < open_playlist_tracks_.size(); ++i) {
                        const auto& tr = open_playlist_tracks_[i];
                        std::string disp = std::to_string(i + 1) + ". " + tr.name;
                        if (!tr.artist.empty())
                            disp += " - " + tr.artist;
                        track_display_names_.push_back(std::move(disp));
                    }
                    track_list_status_ =
                        "共 " + std::to_string(open_playlist_tracks_.size()) + " 首";
                    selected_track_ = 0;
                }
            }
            screen_ref.PostEvent(ftxui::Event::Custom);
        }).detach();
    }

    // 内部：跳转到队列指定索引并播放
    // 根据 QueueEntry::local_path 区分两种模式：
    //   A. 本地文件（local_path 非空）：直接调用 player.Play(local_path)，无需网络请求
    //   B. 在线歌曲（local_path 为空）：
    //     1. 构造缓存文件路径 {cache_dir}/{song_id}.mp3
    //     2. 若缓存文件已存在且非空，跳过 API 请求和下载，直接播放
    //     3. 否则调用 FetchSongUrl 获取 CDN 地址，再调用 DownloadFile 下载到缓存
    //     4. 最终调用 player.Play(cache_path)
    void PlayQueueAt(int idx, ScreenInteractive& screen_ref) {
        int64_t song_id = 0;
        std::string display_name;
        std::string local_path;
        int queue_size = 0;
        {
            const std::lock_guard<std::mutex> lock(queue_mutex_);
            if (idx < 0 || static_cast<size_t>(idx) >= play_queue_.size())
                return;
            current_queue_index_ = idx;
            const auto& entry = play_queue_[static_cast<size_t>(idx)];
            song_id = entry.song_id;
            display_name = entry.display_name;
            local_path = entry.local_path;
            queue_size = static_cast<int>(play_queue_.size());
        }
        // 立即更新状态栏，给用户即时反馈
        queue_status_ = "♪ " + display_name + " (" + std::to_string(idx + 1) + "/" +
                        std::to_string(queue_size) + ")";
        screen_ref.PostEvent(ftxui::Event::Custom);

        // 本地文件模式：直接播放，无需后台下载
        if (!local_path.empty()) {
            auto play_res = player.Play(local_path);
            if (!play_res) {
                queue_status_ = "播放失败: " + play_res.error().message;
                screen_ref.PostEvent(ftxui::Event::Custom);
            }
            return;
        }

        // 在线歌曲模式：后台线程检查缓存、按需下载、最后播放
        std::thread([this, song_id, display_name, idx, queue_size, &screen_ref] {
            // 构造缓存文件路径
            const std::string cache_dir = config::ExpandHomePath(settings.cache_dir);
            const std::string cache_path = cache_dir + "/" + std::to_string(song_id) + ".mp3";

            // 检查缓存文件是否已存在且非空（避免重复下载）
            std::error_code ec;
            const auto file_size = std::filesystem::file_size(cache_path, ec);
            const bool cache_hit = (!ec && file_size > 0);

            if (!cache_hit) {
                // 缓存未命中：先获取 CDN 地址
                network::ApiClient api_client{"", settings.cookies};
                auto url_result = network::FetchSongUrl(api_client, song_id);
                if (!url_result) {
                    queue_status_ = "获取地址失败: " + url_result.error().message;
                    screen_ref.PostEvent(ftxui::Event::Custom);
                    return;
                }
                if (url_result.value().empty()) {
                    // 版权限制，自动跳下一首
                    queue_status_ = "歌曲不可用，跳至下一首...";
                    screen_ref.PostEvent(ftxui::Event::Custom);
                    PlayNextTrack(screen_ref);
                    return;
                }

                // 确保缓存目录存在
                std::filesystem::create_directories(cache_dir, ec);
                if (ec) {
                    queue_status_ = "创建缓存目录失败: " + ec.message();
                    screen_ref.PostEvent(ftxui::Event::Custom);
                    return;
                }

                // 更新状态栏：提示正在下载
                queue_status_ = "下载中: " + display_name + " (" + std::to_string(idx + 1) + "/" +
                                std::to_string(queue_size) + ")";
                screen_ref.PostEvent(ftxui::Event::Custom);

                // 下载音频到本地缓存文件
                auto dl_result = api_client.DownloadFile(url_result.value(), cache_path);
                if (!dl_result) {
                    queue_status_ = "下载失败: " + dl_result.error().message;
                    screen_ref.PostEvent(ftxui::Event::Custom);
                    return;
                }
            }

            // 恢复播放状态栏显示
            queue_status_ = "♪ " + display_name + " (" + std::to_string(idx + 1) + "/" +
                            std::to_string(queue_size) + ")";

            // 使用本地缓存路径播放（miniaudio 仅支持本地文件，不支持 HTTP URL）
            auto play_res = player.Play(cache_path);
            if (!play_res) {
                queue_status_ = "播放失败: " + play_res.error().message;
                screen_ref.PostEvent(ftxui::Event::Custom);
            }
        }).detach();
    }

    // 播放队列下一首（队尾回绕到队头）
    void PlayNextTrack(ScreenInteractive& screen_ref) {
        int next = 0;
        {
            const std::lock_guard<std::mutex> lock(queue_mutex_);
            if (play_queue_.empty())
                return;
            next = (current_queue_index_ + 1) % static_cast<int>(play_queue_.size());
        }
        PlayQueueAt(next, screen_ref);
    }

    // 播放队列上一首（队头回绕到队尾）
    void PlayPrevTrack(ScreenInteractive& screen_ref) {
        int prev = 0;
        {
            const std::lock_guard<std::mutex> lock(queue_mutex_);
            if (play_queue_.empty())
                return;
            const int sz = static_cast<int>(play_queue_.size());
            prev = (current_queue_index_ - 1 + sz) % sz;
        }
        PlayQueueAt(prev, screen_ref);
    }

    // 重建 queue_display_names_（须在持有 queue_mutex_ 时调用）
    // 与 play_queue_ 保持一对一映射，供队列页面 Menu 组件使用
    // 同时将 selected_queue_track_ 边界夹紧，防止越界
    void SyncQueueDisplayNames() {
        queue_display_names_.clear();
        queue_display_names_.reserve(play_queue_.size());
        for (const auto& entry : play_queue_) {
            queue_display_names_.push_back(entry.display_name);
        }
        // 夹紧选中索引：若队列变短则退到最后一项，若为空则归零
        const int sz = static_cast<int>(play_queue_.size());
        if (sz == 0) {
            selected_queue_track_ = 0;
        } else if (selected_queue_track_ >= sz) {
            selected_queue_track_ = sz - 1;
        }
    }

    // 从播放队列中移除指定索引的条目
    // 调整 current_queue_index_ 和 selected_queue_track_，并同步显示名称列表
    void RemoveFromQueue(int idx, ScreenInteractive& screen_ref) {
        {
            const std::lock_guard<std::mutex> lock(queue_mutex_);
            if (idx < 0 || static_cast<size_t>(idx) >= play_queue_.size())
                return;

            play_queue_.erase(play_queue_.begin() + static_cast<ptrdiff_t>(idx));

            // 调整「正在播放」指针
            if (play_queue_.empty()) {
                current_queue_index_ = -1;
                queue_status_.clear();
            } else if (idx < current_queue_index_) {
                // 删除的是当前播放项前面的条目，索引前移一位
                --current_queue_index_;
            } else if (idx == current_queue_index_) {
                // 删除的是当前播放项，指向下一首（如已是末尾则指向新末尾）
                if (current_queue_index_ >= static_cast<int>(play_queue_.size())) {
                    current_queue_index_ = static_cast<int>(play_queue_.size()) - 1;
                }
                // current_queue_index_ 现在指向原来的下一首，无需额外操作
            }
            // idx > current_queue_index_：删除的是后面的条目，当前播放指针不变

            SyncQueueDisplayNames();
        }
        screen_ref.PostEvent(ftxui::Event::Custom);
    }

    // 内部：用给定曲目列表替换整个播放队列，并从 start_index 开始播放
    void ReplaceQueueWithTracks(const std::vector<network::Track>& tracks, int start_index,
                                ScreenInteractive& screen_ref) {
        {
            const std::lock_guard<std::mutex> lock(queue_mutex_);
            play_queue_.clear();
            play_queue_.reserve(tracks.size());
            for (const auto& tr : tracks) {
                std::string name = tr.name;
                if (!tr.artist.empty())
                    name += " - " + tr.artist;
                play_queue_.push_back(QueueEntry{tr.id, std::move(name), ""});
            }
            current_queue_index_ = -1;
            // 将列表光标置于起始播放位置，方便用户在播放列表页面定位
            selected_queue_track_ =
                std::clamp(start_index, 0, std::max(0, static_cast<int>(play_queue_.size()) - 1));
            SyncQueueDisplayNames();
        }
        PlayQueueAt(start_index, screen_ref);
    }

    // Shift+Enter 在歌单列表：用整个歌单替换播放队列并从头播放
    void LoadAndReplaceQueue(ScreenInteractive& screen_ref) {
        int64_t playlist_id = 0;
        {
            const std::lock_guard<std::mutex> lock(playlists_mutex_);
            if (user_playlists_.empty())
                return;
            const auto idx = static_cast<size_t>(selected_playlist_);
            if (idx >= user_playlists_.size())
                return;
            playlist_id = user_playlists_[idx].id;
        }
        // 优先使用缓存
        std::vector<network::Track> cached;
        {
            const std::lock_guard<std::mutex> lock(tracks_mutex_);
            if (playlist_id == open_playlist_id_ && !open_playlist_tracks_.empty())
                cached = open_playlist_tracks_;
        }
        if (!cached.empty()) {
            ReplaceQueueWithTracks(cached, 0, screen_ref);
            return;
        }
        // 无缓存，后台请求
        queue_status_ = "正在加载歌单...";
        screen_ref.PostEvent(ftxui::Event::Custom);
        std::thread([this, playlist_id, &screen_ref] {
            network::ApiClient api_client{"", settings.cookies};
            auto result = network::FetchPlaylistTracks(api_client, playlist_id);
            if (!result) {
                queue_status_ = "加载失败: " + result.error().message;
                screen_ref.PostEvent(ftxui::Event::Custom);
                return;
            }
            ReplaceQueueWithTracks(result.value(), 0, screen_ref);
        }).detach();
    }

    // Enter 在曲目列表：用整个歌单替换队列，从选中曲目开始播放
    void PlayTrackFromDetail(ScreenInteractive& screen_ref) {
        std::vector<network::Track> snapshot;
        int start = 0;
        {
            const std::lock_guard<std::mutex> lock(tracks_mutex_);
            if (open_playlist_tracks_.empty())
                return;
            snapshot = open_playlist_tracks_;
            start = selected_track_;
        }
        ReplaceQueueWithTracks(snapshot, start, screen_ref);
    }

    // Shift+Enter 在曲目列表：将选中曲目追加到队列末尾；若队列为空则立即播放
    void AppendTrackFromDetail(ScreenInteractive& screen_ref) {
        network::Track tr;
        {
            const std::lock_guard<std::mutex> lock(tracks_mutex_);
            if (open_playlist_tracks_.empty())
                return;
            const auto idx = static_cast<size_t>(selected_track_);
            if (idx >= open_playlist_tracks_.size())
                return;
            tr = open_playlist_tracks_[idx];
        }
        std::string name = tr.name;
        if (!tr.artist.empty())
            name += " - " + tr.artist;
        bool was_empty = false;
        int new_idx = 0;
        {
            const std::lock_guard<std::mutex> lock(queue_mutex_);
            was_empty = play_queue_.empty();
            play_queue_.push_back(QueueEntry{tr.id, name, ""});
            new_idx = static_cast<int>(play_queue_.size()) - 1;
            SyncQueueDisplayNames();
        }
        if (was_empty) {
            PlayQueueAt(new_idx, screen_ref);
        } else {
            queue_status_ = "已加入队列: " + name;
            screen_ref.PostEvent(ftxui::Event::Custom);
        }
    }

    // 内部：用本地文件列表替换整个播放队列，并从 start_index 开始播放
    // 与 ReplaceQueueWithTracks 对应，但为本地文件创建 QueueEntry（song_id=0, local_path 非空）
    void ReplaceQueueWithLocalTracks(const std::vector<library::LocalTrack>& tracks,
                                     int start_index, ScreenInteractive& screen_ref) {
        {
            const std::lock_guard<std::mutex> lock(queue_mutex_);
            play_queue_.clear();
            play_queue_.reserve(tracks.size());
            for (const auto& tr : tracks) {
                play_queue_.push_back(QueueEntry{0, tr.file_name, tr.file_path});
            }
            current_queue_index_ = -1;
            selected_queue_track_ =
                std::clamp(start_index, 0, std::max(0, static_cast<int>(play_queue_.size()) - 1));
            SyncQueueDisplayNames();
        }
        PlayQueueAt(start_index, screen_ref);
    }

    // Enter 在本地文件页面：用全部本地文件替换队列，从选中文件开始播放
    void PlayLocalTrackFromList(ScreenInteractive& screen_ref) {
        if (local_tracks.empty())
            return;
        const int start = selected_local_track;
        ReplaceQueueWithLocalTracks(local_tracks, start, screen_ref);
    }

    // Shift+Enter 在本地文件页面：将选中本地文件追加到队列末尾；若队列为空则立即播放
    void AppendLocalTrackToQueue(ScreenInteractive& screen_ref) {
        if (local_tracks.empty())
            return;
        const auto idx = static_cast<size_t>(selected_local_track);
        if (idx >= local_tracks.size())
            return;
        const auto& track = local_tracks[idx];
        const std::string name = track.file_name;
        bool was_empty = false;
        int new_idx = 0;
        {
            const std::lock_guard<std::mutex> lock(queue_mutex_);
            was_empty = play_queue_.empty();
            play_queue_.push_back(QueueEntry{0, name, track.file_path});
            new_idx = static_cast<int>(play_queue_.size()) - 1;
            SyncQueueDisplayNames();
        }
        if (was_empty) {
            PlayQueueAt(new_idx, screen_ref);
        } else {
            queue_status_ = "已加入队列: " + name;
            screen_ref.PostEvent(ftxui::Event::Custom);
        }
    }

    // ── 渲染方法 ──

    // 歌单列表子视图（playlist_view_index_ == 0）
    // 调用方须已持有 playlists_mutex_
    // 选中项附加 focus 装饰器，配合 yframe 实现自动滚动
    auto BuildPlaylistListContent() -> Element {
        Elements header;
        header.push_back(text("我的歌单") | bold | center);
        header.push_back(separator());
        header.push_back(text(playlist_status_.empty() ? "请先登录" : playlist_status_) | dim);

        // 可滚动的歌单条目列表
        Elements list_items;
        if (!user_playlists_.empty()) {
            for (size_t i = 0; i < user_playlists_.size(); ++i) {
                auto row = text(playlist_display_names_[i]) | flex;
                if (static_cast<int>(i) == selected_playlist_) {
                    row = row | inverted | focus;
                }
                list_items.push_back(row);
            }
        }

        Elements footer;
        footer.push_back(separator());
        footer.push_back(text("Enter: 打开  Shift+Enter: 替换队列播放  r: 刷新") | dim);

        if (list_items.empty()) {
            return vbox({
                       vbox(std::move(header)),
                       filler(),
                       vbox(std::move(footer)),
                   }) |
                   flex;
        }

        return vbox({
                   vbox(std::move(header)),
                   separator(),
                   vbox(std::move(list_items)) | yframe | vscroll_indicator | flex,
                   vbox(std::move(footer)),
               }) |
               flex;
    }

    // 曲目列表子视图（playlist_view_index_ == 1）
    // 调用方须已持有 tracks_mutex_
    // 选中项附加 focus 装饰器，配合 yframe 实现自动滚动
    auto BuildTrackListContent() -> Element {
        Elements header;
        header.push_back(text(open_playlist_name_) | bold | center);
        header.push_back(separator());
        header.push_back(text(track_list_status_.empty() ? "正在加载..." : track_list_status_) |
                         dim);

        // 可滚动的曲目条目列表
        Elements list_items;
        if (!track_display_names_.empty()) {
            for (size_t i = 0; i < track_display_names_.size(); ++i) {
                auto row = text(track_display_names_[i]) | flex;
                if (static_cast<int>(i) == selected_track_) {
                    row = row | inverted | focus;
                }
                list_items.push_back(row);
            }
        }

        Elements footer;
        footer.push_back(separator());
        footer.push_back(text("Enter: 播放  Shift+Enter: 加入队列  Esc: 返回") | dim);

        if (list_items.empty()) {
            return vbox({
                       vbox(std::move(header)),
                       filler(),
                       vbox(std::move(footer)),
                   }) |
                   flex;
        }

        return vbox({
                   vbox(std::move(header)),
                   separator(),
                   vbox(std::move(list_items)) | yframe | vscroll_indicator | flex,
                   vbox(std::move(footer)),
               }) |
               flex;
    }

    // 我的歌单页面入口：根据 playlist_view_index_ 分发到对应子视图
    auto BuildMyPlaylistsContent() -> Element {
        if (playlist_view_index_ == 1) {
            const std::lock_guard<std::mutex> lock(tracks_mutex_);
            return BuildTrackListContent();
        }
        const std::lock_guard<std::mutex> lock(playlists_mutex_);
        return BuildPlaylistListContent();
    }

    // 播放列表页面：展示当前播放队列
    // ♪ 标记正在播放的条目，高亮反显当前光标选中项（两者可不同）
    // 选中项附加 focus 装饰器，配合 yframe 实现自动滚动
    auto BuildQueueContent() -> Element {
        Elements header;
        header.push_back(text("播放列表") | bold | center);
        header.push_back(separator());

        const std::lock_guard<std::mutex> lock(queue_mutex_);

        if (play_queue_.empty()) {
            // 队列为空时给出引导提示
            Elements items;
            items.push_back(vbox(std::move(header)));
            items.push_back(filler());
            items.push_back(text("播放列表为空") | dim | center);
            items.push_back(text("在「我的歌单」曲目列表中按 Enter 加载歌单") | dim | center);
            items.push_back(filler());
            items.push_back(separator());
            items.push_back(text("Enter/Shift+Enter: 跳播  d: 删除  [/]: 上/下一首") | dim);
            return vbox(std::move(items)) | flex;
        }

        header.push_back(text("共 " + std::to_string(play_queue_.size()) + " 首") | dim);
        header.push_back(separator());

        // 可滚动的条目列表
        Elements list_items;
        for (size_t i = 0; i < play_queue_.size(); ++i) {
            const bool is_now_playing = (static_cast<int>(i) == current_queue_index_);
            const bool is_selected = (static_cast<int>(i) == selected_queue_track_);

            // 左侧图标：♪ 表示正在播放，空格占位保持对齐
            auto icon = text(is_now_playing ? "♪ " : "  ");
            auto name = text(play_queue_[i].display_name) | flex;

            auto row = hbox({std::move(icon), std::move(name)});

            if (is_selected && is_now_playing) {
                // 光标与播放项重合：高亮 + 青色
                row = row | inverted | color(Color::Cyan);
            } else if (is_selected) {
                row = row | inverted;
            } else if (is_now_playing) {
                // 仅播放中：绿色标记
                row = row | color(Color::Green);
            }

            // 选中项附加 focus，使 yframe 自动滚动到此处
            if (is_selected) {
                row = row | focus;
            }
            list_items.push_back(row);
        }

        // 底部固定提示栏
        Elements footer;
        footer.push_back(separator());
        footer.push_back(text("Enter/Shift+Enter: 跳播  d: 删除  [/]: 上/下一首") | dim);

        return vbox({
                   vbox(std::move(header)),
                   vbox(std::move(list_items)) | yframe | vscroll_indicator | flex,
                   vbox(std::move(footer)),
               }) |
               flex;
    }

    // 使用系统默认编辑器打开配置文件    // 流程：
    //   1. 通过 WithRestoredIO 临时卸载 FTXUI 的终端钩子（raw mode / alt-screen）
    //   2. 在恢复后的普通终端中同步运行编辑器（vi/nano/gedit 等均可）
    //   3. 编辑器退出后，FTXUI 自动重新接管终端，TUI 继续运行
    void OpenConfigFile() {
        // 优先使用用户偏好的终端编辑器，依次尝试 $EDITOR、$VISUAL、vi
        const char* editor = std::getenv("EDITOR");
        if (editor == nullptr) {
            editor = std::getenv("VISUAL");
        }
        if (editor == nullptr) {
            editor = "vi";
        }

        const std::string editor_name = editor;

        // 用双引号包裹配置文件路径，防止路径中含空格时命令解析出错
        const std::string cmd = editor_name + " \"" + config_path + "\"";

        // WithRestoredIO：将系统调用包装为一个 Closure，调用时 FTXUI 会：
        //   - 先还原终端至普通模式（关闭 raw mode，退出 alt-screen）
        //   - 执行 Closure（即阻塞运行编辑器）
        //   - 编辑器退出后，重新进入 TUI 模式并刷新屏幕
        screen.WithRestoredIO([cmd] {
            std::system(cmd.c_str());  // NOLINT(concurrency-mt-unsafe)
        })();

        open_config_status = "已使用 " + editor_name + " 打开配置文件";
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
    // 选中项附加 focus 装饰器，配合 yframe 实现自动滚动
    auto BuildLocalFilesContent() -> Element {
        Elements header;
        header.push_back(
            text("本地音乐库: " + config::ExpandHomePath(settings.music_library_path)) | bold);
        header.push_back(separator());

        if (local_tracks.empty()) {
            Elements items;
            items.push_back(vbox(std::move(header)));
            items.push_back(text(local_scan_status.empty() ? "暂无音乐文件" : local_scan_status) |
                            dim);
            items.push_back(filler());
            items.push_back(separator());
            items.push_back(text("Enter: 播放  Shift+Enter: 加入队列  r: 刷新") | dim);
            return vbox(std::move(items)) | flex;
        }

        header.push_back(text(local_scan_status) | dim);
        header.push_back(separator());

        // 可滚动的文件列表
        Elements list_items;
        for (size_t i = 0; i < local_tracks.size(); ++i) {
            const auto& track = local_tracks[i];
            auto line = hbox({
                text(std::to_string(i + 1) + ". "),
                text(track.file_name) | flex,
                text(" [" + track.extension + "]") | dim,
            });
            if (static_cast<int>(i) == selected_local_track) {
                line = line | inverted | focus;
            }
            list_items.push_back(line);
        }

        // 底部操作提示
        Elements footer;
        footer.push_back(separator());
        footer.push_back(text("Enter: 播放  Shift+Enter: 加入队列  r: 刷新") | dim);

        return vbox({
                   vbox(std::move(header)),
                   vbox(std::move(list_items)) | yframe | vscroll_indicator | flex,
                   vbox(std::move(footer)),
               }) |
               flex;
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

    // 构建账密登录表单区域
    // 包含手机号输入框、密码输入框（掩码）以及确认/取消按钮
    // 仅在 password_form_tab_ == 1 时由 BuildLoginSection() 调用
    auto BuildPasswordForm() -> Element {
        if (!static_cast<bool>(input_phone)) {
            return text("表单未初始化") | dim;
        }
        Elements items;
        items.push_back(text("账密登录") | bold | center);
        items.push_back(separator());
        items.push_back(hbox({text("手机号: ") | bold, input_phone->Render() | flex}));
        items.push_back(hbox({text("密  码: ") | bold, input_password->Render() | flex}));
        items.push_back(hbox({
            filler(),
            btn_confirm_login->Render(),
            text("  "),
            btn_cancel_form->Render(),
        }));
        return vbox(std::move(items)) | border;
    }

    // 构建 Cookie 登录表单区域
    // 包含单个 Cookie 输入框以及确认/取消按钮
    // 仅在 password_form_tab_ == 2 时由 BuildLoginSection() 调用
    auto BuildCookieForm() -> Element {
        if (!static_cast<bool>(input_cookie)) {
            return text("表单未初始化") | dim;
        }
        Elements items;
        items.push_back(text("Cookie 登录") | bold | center);
        items.push_back(separator());
        items.push_back(text("请从浏览器中复制网易云音乐的 Cookie（须包含 MUSIC_U）") | dim);
        items.push_back(hbox({text("Cookie: ") | bold, input_cookie->Render() | flex}));
        items.push_back(hbox({
            filler(),
            btn_confirm_cookie_login->Render(),
            text("  "),
            btn_cancel_cookie_form->Render(),
        }));
        return vbox(std::move(items)) | border;
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

        // 优先显示登录表单（任何状态下均可切换）
        if (password_form_tab_ == 1 && btns_ready) {
            return BuildPasswordForm();
        }
        if (password_form_tab_ == 2 && btns_ready) {
            return BuildCookieForm();
        }

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
                    items.push_back(hbox({text("  "), btn_scan_login->Render(), text("  "),
                                          btn_password_login->Render(), text("  "),
                                          btn_cookie_login->Render()}) |
                                    center);
                }
                break;
            }

            case auth::LoginState::kVerifying: {
                // 校验已保存的 cookies，允许跳过直接扫码或账密登录
                Elements row;
                row.push_back(text("正在验证登录状态...") | dim);
                if (btns_ready) {
                    row.push_back(text("  "));
                    row.push_back(btn_refresh->Render());
                    row.push_back(text("  "));
                    row.push_back(btn_scan_login->Render());
                    row.push_back(text("  "));
                    row.push_back(btn_password_login->Render());
                    row.push_back(text("  "));
                    row.push_back(btn_cookie_login->Render());
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
                    row.push_back(btn_password_login->Render());
                    row.push_back(text("  "));
                    row.push_back(btn_cookie_login->Render());
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
                    row.push_back(btn_password_login->Render());
                    row.push_back(text("  "));
                    row.push_back(btn_cookie_login->Render());
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
                    row.push_back(text("  "));
                    row.push_back(btn_password_login->Render());
                    row.push_back(text("  "));
                    row.push_back(btn_cookie_login->Render());
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
    // 播放/暂停时：状态图标 | 已播放时间 ████░░ 总时长 | 队列信息 | 音量
    // 其他状态：状态文字 + 队列信息 + 音量
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

            Elements bar;
            bar.push_back(text(state_icon) | color(Color::Yellow));
            bar.push_back(text(FormatTime(pos)) | dim);
            bar.push_back(text(" "));
            bar.push_back(gauge(ratio) | flex | color(Color::Cyan));
            bar.push_back(text(" "));
            bar.push_back(text(FormatTime(dur)) | dim);
            bar.push_back(text("  "));
            if (!queue_status_.empty()) {
                bar.push_back(text(queue_status_) | color(Color::Green));
                bar.push_back(text("  "));
            }
            bar.push_back(text(vol_text) | dim);
            return hbox(std::move(bar)) | border;
        }

        if (state == player::PlayerState::kLoading) {
            Elements bar;
            bar.push_back(text("⏳ 加载中") | dim | flex);
            if (!queue_status_.empty()) {
                bar.push_back(text(queue_status_) | color(Color::Green));
                bar.push_back(text("  "));
            }
            bar.push_back(text(vol_text) | dim);
            return hbox(std::move(bar)) | border;
        }

        // kStopped
        Elements bar;
        bar.push_back(text("■ 停止") | dim | flex);
        if (!queue_status_.empty()) {
            bar.push_back(text(queue_status_) | dim);
            bar.push_back(text("  "));
        }
        bar.push_back(text(vol_text) | dim);
        return hbox(std::move(bar)) | border;
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
    // 同时，当状态变为 kLoggedIn 时自动触发歌单加载
    impl_->login_manager.SetOnStateChange([this] {
        if (impl_->login_manager.GetState() == auth::LoginState::kLoggedIn) {
            impl_->LoadUserPlaylists(impl_->screen);
        }
        impl_->screen.PostEvent(ftxui::Event::Custom);
    });

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

    // 账密登录按钮：点击后切换至账密输入表单
    impl_->btn_password_login = Impl::MakeInlineButton("账密登录", [this] {
        impl_->password_form_tab_ = 1;
        impl_->phone_input_value_.clear();
        impl_->password_input_value_.clear();
        impl_->screen.PostEvent(ftxui::Event::Custom);
    });

    // 手机号输入框
    {
        InputOption opt;
        opt.placeholder = "请输入手机号";
        impl_->input_phone = Input(&impl_->phone_input_value_, opt);
    }

    // 密码输入框（掩码显示，Enter 键直接提交登录）
    {
        InputOption opt;
        opt.placeholder = "请输入密码";
        opt.password = true;
        opt.on_enter = [this] {
            impl_->login_manager.StartPasswordLogin(impl_->phone_input_value_,
                                                    impl_->password_input_value_);
            impl_->password_form_tab_ = 0;
        };
        impl_->input_password = Input(&impl_->password_input_value_, opt);
    }

    // 确认登录按钮
    impl_->btn_confirm_login = Impl::MakeInlineButton("确认登录", [this] {
        impl_->login_manager.StartPasswordLogin(impl_->phone_input_value_,
                                                impl_->password_input_value_);
        impl_->password_form_tab_ = 0;
    });

    // 取消按钮：清空输入并返回正常按钮行
    impl_->btn_cancel_form = Impl::MakeInlineButton("取消", [this] {
        impl_->password_form_tab_ = 0;
        impl_->phone_input_value_.clear();
        impl_->password_input_value_.clear();
        impl_->screen.PostEvent(ftxui::Event::Custom);
    });

    // Cookie 登录按钮：点击后切换至 Cookie 输入表单
    impl_->btn_cookie_login = Impl::MakeInlineButton("Cookie登录", [this] {
        impl_->password_form_tab_ = 2;
        impl_->cookie_input_value_.clear();
        impl_->screen.PostEvent(ftxui::Event::Custom);
    });

    // Cookie 输入框（Enter 键直接提交）
    {
        InputOption opt;
        opt.placeholder = "请粘贴包含 MUSIC_U 的 Cookie 字符串";
        opt.on_enter = [this] {
            impl_->login_manager.StartCookieLogin(impl_->cookie_input_value_);
            impl_->password_form_tab_ = 0;
        };
        impl_->input_cookie = Input(&impl_->cookie_input_value_, opt);
    }

    // Cookie 表单确认登录按钮
    impl_->btn_confirm_cookie_login = Impl::MakeInlineButton("确认登录", [this] {
        impl_->login_manager.StartCookieLogin(impl_->cookie_input_value_);
        impl_->password_form_tab_ = 0;
    });

    // Cookie 表单取消按钮
    impl_->btn_cancel_cookie_form = Impl::MakeInlineButton("取消", [this] {
        impl_->password_form_tab_ = 0;
        impl_->cookie_input_value_.clear();
        impl_->screen.PostEvent(ftxui::Event::Custom);
    });

    // Tab=0：正常按钮行（扫码登录、账密登录、Cookie登录、刷新、退出登录）
    auto normal_buttons = Container::Horizontal({
        impl_->btn_scan_login,
        impl_->btn_password_login,
        impl_->btn_cookie_login,
        impl_->btn_refresh,
        impl_->btn_logout,
    });
    // Tab=1：账密登录表单（手机号输入框、密码输入框、确认/取消按钮）
    auto form_container = Container::Vertical({
        impl_->input_phone,
        impl_->input_password,
        Container::Horizontal({impl_->btn_confirm_login, impl_->btn_cancel_form}),
    });
    // Tab=2：Cookie 登录表单（Cookie 输入框、确认/取消按钮）
    auto cookie_form_container = Container::Vertical({
        impl_->input_cookie,
        Container::Horizontal({impl_->btn_confirm_cookie_login, impl_->btn_cancel_cookie_form}),
    });
    // Container::Tab 确保只有激活标签的子组件接收焦点与事件
    auto settings_buttons = Container::Tab({normal_buttons, form_container, cookie_form_container},
                                           &impl_->password_form_tab_);
    // 构建左侧菜单组件
    auto menu = impl_->BuildMenu();

    // 播放列表页面：queue_menu 负责上下键导航（维护 selected_queue_track_），
    // BuildQueueContent 提供自定义渲染（♪ 播放标记、高亮、操作提示）
    auto queue_menu = Menu(&impl_->queue_display_names_, &impl_->selected_queue_track_);
    auto queue_page = Renderer(queue_menu, [this] { return impl_->BuildQueueContent(); });

    // 本地文件列表的 Menu 组件（处理上下键导航，同步 selected_local_track）
    auto local_menu = Menu(&impl_->local_file_names, &impl_->selected_local_track);

    // 使用 Renderer(component, fn) 为每个页面包装组件树与渲染树：
    //   - component：负责焦点管理与事件处理（键盘导航、按钮激活等）
    //   - fn：负责渲染输出，可读取 component 管理的状态（如选中索引）
    // 这样确保组件树（事件/焦点）与渲染树（Element）保持对齐，
    // 解决直接调用 BuildXxx() 导致的渲染链断裂、按钮无法获取焦点的问题。

    // 占位页面：搜索功能待实现
    auto placeholder_search = Renderer([this] { return impl_->BuildPlaceholderContent(); });

    // 我的歌单页面：两层导航
    //   - playlists_menu：歌单列表视图的上下键导航（维护 selected_playlist_）
    //   - tracks_menu：曲目列表视图的上下键导航（维护 selected_track_）
    //   - playlists_nav：Container::Tab 根据 playlist_view_index_ 决定哪个 Menu 获得焦点
    // Renderer 包装后提供统一的自定义渲染（高亮、状态提示等）
    auto playlists_menu = Menu(&impl_->playlist_display_names_, &impl_->selected_playlist_);
    auto tracks_menu = Menu(&impl_->track_display_names_, &impl_->selected_track_);
    auto playlists_nav =
        Container::Tab({playlists_menu, tracks_menu}, &impl_->playlist_view_index_);
    auto my_playlists_page =
        Renderer(playlists_nav, [this] { return impl_->BuildMyPlaylistsContent(); });

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
            queue_page,          // 0: 播放列表（当前播放队列）
            placeholder_search,  // 1: 搜索（待实现）
            my_playlists_page,   // 2: 我的歌单（带 playlists_menu 交互）
            local_files_page,    // 3: 本地文件（带 local_menu 交互）
            settings_page,       // 4: 设置（带 settings_buttons 交互）
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
    // Shift+Enter 在 Kitty 协议终端下产生 \x1b[13;2u
    const Event kShiftEnter = Event::Special("\x1b[13;2u");
    auto main_component = CatchEvent(renderer, [this, kShiftEnter](Event event) {
        // 当登录表单处于激活状态时（账密表单或 Cookie 表单），
        // 所有字符键事件直接传递给输入框处理，
        // 避免 q/e/[/] 等全局快捷键在用户输入时意外触发
        if (impl_->password_form_tab_ != 0 && event.is_character()) {
            return false;
        }

        // q 键退出程序
        if (event == Event::Character('q')) {
            impl_->screen.Exit();
            return true;
        }

        // ── 全局：上下曲切换（任何页面均有效）──
        if (event == Event::Character('[')) {
            impl_->PlayPrevTrack(impl_->screen);
            return true;
        }
        if (event == Event::Character(']')) {
            impl_->PlayNextTrack(impl_->screen);
            return true;
        }

        // ── 播放列表页面（当前播放队列）──
        if (impl_->selected_menu == 0) {
            if (event == Event::Return || event == kShiftEnter) {
                // Enter / Shift+Enter：跳转并播放选中条目
                int idx = impl_->selected_queue_track_;
                bool valid = false;
                {
                    const std::lock_guard<std::mutex> lock(impl_->queue_mutex_);
                    valid = (idx >= 0 && static_cast<size_t>(idx) < impl_->play_queue_.size());
                }
                if (valid) {
                    impl_->PlayQueueAt(idx, impl_->screen);
                }
                return true;
            }
            if (event == Event::Character('d')) {
                // d：删除选中条目
                impl_->RemoveFromQueue(impl_->selected_queue_track_, impl_->screen);
                return true;
            }
        }

        // ── 我的歌单页面 ──
        if (impl_->selected_menu == 2) {
            if (impl_->playlist_view_index_ == 0) {
                // 歌单列表视图
                if (event == Event::Return) {
                    impl_->OpenPlaylistDetail(impl_->screen);
                    return true;
                }
                if (event == kShiftEnter) {
                    impl_->LoadAndReplaceQueue(impl_->screen);
                    return true;
                }
                if (event == Event::Character('r')) {
                    impl_->LoadUserPlaylists(impl_->screen);
                    return true;
                }
            } else {
                // 曲目列表视图
                if (event == Event::Return) {
                    impl_->PlayTrackFromDetail(impl_->screen);
                    return true;
                }
                if (event == kShiftEnter) {
                    impl_->AppendTrackFromDetail(impl_->screen);
                    return true;
                }
                // Esc 返回歌单列表
                if (event == Event::Escape) {
                    impl_->playlist_view_index_ = 0;
                    impl_->screen.PostEvent(ftxui::Event::Custom);
                    return true;
                }
            }
        }

        // ── 本地文件页面 ──
        if (impl_->selected_menu == 3) {
            // Enter：用全部本地文件替换队列，从选中文件开始播放
            if (event == Event::Return) {
                impl_->PlayLocalTrackFromList(impl_->screen);
                return true;
            }
            // Shift+Enter：将选中本地文件追加到队列末尾
            if (event == kShiftEnter) {
                impl_->AppendLocalTrackToQueue(impl_->screen);
                return true;
            }
            if (event == Event::Character('r')) {
                impl_->RefreshLocalLibrary();
                return true;
            }
        }

        // ── 设置页面 ──
        if (impl_->selected_menu == 4) {
            if (event == Event::Character('e')) {
                impl_->OpenConfigFile();
                return true;
            }
        }

        return false;
    });

    // 启动 500ms 定时刷新线程，驱动播放进度条更新
    // 线程通过 PostEvent(Custom) 触发 FTXUI 重绘，Loop 返回后通过 stop_refresh 标志退出
    // 同时在每次刷新时检测歌曲自然结束，若结束则自动跳至队列下一首
    std::atomic<bool> stop_refresh{false};
    std::thread refresh_thread([&] {
        while (!stop_refresh.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            if (!stop_refresh.load()) {
                // ── 自动连续播放：检测歌曲是否自然播放结束 ──
                // 条件：播放器仍报告 kPlaying（状态未被手动改变）且音频已到结尾
                if (impl_->player.GetState() == player::PlayerState::kPlaying &&
                    impl_->player.IsAtEnd()) {
                    // 立即停止，防止 500ms 内重复触发
                    impl_->player.Stop();
                    // 检查是否有队列可切换
                    bool has_queue = false;
                    {
                        const std::lock_guard<std::mutex> lock(impl_->queue_mutex_);
                        has_queue = !impl_->play_queue_.empty();
                    }
                    if (has_queue) {
                        impl_->PlayNextTrack(impl_->screen);
                    }
                }
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
