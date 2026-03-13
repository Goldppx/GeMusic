#ifndef GEMUSIC_LYRICS_LYRICS_MANAGER_H
#define GEMUSIC_LYRICS_LYRICS_MANAGER_H

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "gemusic/lyrics/lrc_parser.h"
#include "gemusic/network/api_client.h"

namespace gemusic::lyrics {

// 歌词加载状态
enum class LyricsState {
    kIdle,     // 尚未加载（初始状态）
    kLoading,  // 正在后台加载
    kLoaded,   // 加载成功，lines 有效
    kError,    // 加载失败（无歌词可用）
};

// 歌词管理器：负责异步加载歌词（优先本地 .lrc → 在线网易云）
// 线程安全：所有公开方法可从任意线程调用
class LyricsManager {
   public:
    LyricsManager() = default;

    // 禁止拷贝（含有互斥锁）
    LyricsManager(const LyricsManager&) = delete;
    LyricsManager& operator=(const LyricsManager&) = delete;

    // 允许移动
    LyricsManager(LyricsManager&&) = default;
    LyricsManager& operator=(LyricsManager&&) = default;

    ~LyricsManager() = default;

    // 异步加载指定歌曲的歌词
    // 加载顺序：本地同名 .lrc 文件 → 在线歌词（song_id > 0 时）
    // 调用此方法会取消上一次的加载请求（通过 generation_ 版本号控制）
    // 参数:
    //   song_id    - 歌曲的网易云 ID（0 表示纯本地文件，跳过在线加载）
    //   local_path - 本地音频文件路径（用于查找同名 .lrc 文件，可为空）
    //   client     - 已配置 cookies 的 HTTP 客户端引用（仅在线模式下使用）
    //   on_loaded  - 加载完成后的回调（在后台线程中调用，须线程安全）
    void LoadAsync(int64_t song_id, const std::string& local_path, network::ApiClient& client,
                   std::function<void()> on_loaded = nullptr);

    // 根据当前播放位置（毫秒）返回应高亮的歌词行索引（-1 表示尚未到第一行）
    // 线程安全，可从渲染线程调用
    auto GetCurrentLineIndex(uint32_t position_ms) const -> int;

    // 获取全部歌词行（线程安全，返回副本）
    auto GetLines() const -> std::vector<LrcLine>;

    // 获取当前加载状态
    auto GetState() const -> LyricsState;

    // 清空歌词数据，回到 kIdle 状态（换歌前调用）
    void Clear();

   private:
    mutable std::mutex mutex_;

    // 当前歌词行（仅在 state_ == kLoaded 时有效）
    std::vector<LrcLine> lines_;

    // 当前加载状态
    LyricsState state_ = LyricsState::kIdle;

    // 版本号：每次调用 LoadAsync 自增；后台线程检查版本，若不匹配则丢弃结果
    uint32_t generation_ = 0;
};

}  // namespace gemusic::lyrics

#endif  // GEMUSIC_LYRICS_LYRICS_MANAGER_H
