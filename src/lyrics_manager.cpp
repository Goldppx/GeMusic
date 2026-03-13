#include "gemusic/lyrics/lyrics_manager.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

#include <spdlog/spdlog.h>

#include "gemusic/network/netease_api.h"

namespace gemusic::lyrics {

void LyricsManager::LoadAsync(int64_t song_id, const std::string& local_path,
                              network::ApiClient& client, std::function<void()> on_loaded) {
    uint32_t gen = 0;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        // 新一轮加载：版本号自增，令旧的后台线程结果作废
        gen = ++generation_;
        state_ = LyricsState::kLoading;
        lines_.clear();
    }

    // 在独立线程中完成加载，避免阻塞 UI 线程
    std::thread([this, gen, song_id, local_path, &client, cb = std::move(on_loaded)]() mutable {
        // ── 步骤 1：尝试读取本地 .lrc 文件 ──
        // 规则：将音频文件路径的扩展名替换为 .lrc，若文件存在则读取
        if (!local_path.empty()) {
            std::filesystem::path audio_path(local_path);
            std::filesystem::path lrc_path = audio_path.replace_extension(".lrc");

            std::error_code ec;
            if (std::filesystem::exists(lrc_path, ec) && !ec) {
                std::ifstream lrc_file(lrc_path);
                if (lrc_file.is_open()) {
                    std::ostringstream ss;
                    ss << lrc_file.rdbuf();
                    const std::string lrc_text = ss.str();

                    auto parsed = ParseLrc(lrc_text);
                    if (!parsed.empty()) {
                        spdlog::info("LyricsManager: 从本地 .lrc 加载歌词 ({} 行) - {}",
                                     parsed.size(), lrc_path.string());
                        // 检查版本，确认未被更新的 LoadAsync 调用覆盖
                        {
                            const std::lock_guard<std::mutex> lock(mutex_);
                            if (generation_ != gen) {
                                return;  // 已被新请求取代，丢弃结果
                            }
                            lines_ = std::move(parsed);
                            state_ = LyricsState::kLoaded;
                        }
                        if (cb) {
                            cb();
                        }
                        return;
                    }
                }
            }
        }

        // ── 步骤 2：在线加载（仅当 song_id > 0 时）──
        if (song_id > 0) {
            auto lyric_result = network::FetchSongLyrics(client, song_id);
            if (lyric_result && !lyric_result.value().empty()) {
                auto parsed = ParseLrc(lyric_result.value());
                if (!parsed.empty()) {
                    spdlog::info("LyricsManager: 从网络加载歌词 ({} 行) - song_id={}",
                                 parsed.size(), song_id);
                    {
                        const std::lock_guard<std::mutex> lock(mutex_);
                        if (generation_ != gen) {
                            return;  // 已被新请求取代
                        }
                        lines_ = std::move(parsed);
                        state_ = LyricsState::kLoaded;
                    }
                    if (cb) {
                        cb();
                    }
                    return;
                }
            } else if (!lyric_result) {
                spdlog::warn("LyricsManager: 在线歌词加载失败 - {}", lyric_result.error().message);
            }
        }

        // ── 步骤 3：加载失败（无歌词）──
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            if (generation_ != gen) {
                return;
            }
            state_ = LyricsState::kError;
        }
        if (cb) {
            cb();
        }
    }).detach();
}

auto LyricsManager::GetCurrentLineIndex(uint32_t position_ms) const -> int {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (lines_.empty()) {
        return -1;
    }

    // 二分查找：找到最后一个 time_ms <= position_ms 的行
    // 使用 upper_bound 找到第一个 time_ms > position_ms 的迭代器，然后退一步
    auto it = std::upper_bound(lines_.begin(), lines_.end(), position_ms,
                               [](uint32_t ms, const LrcLine& line) { return ms < line.time_ms; });

    if (it == lines_.begin()) {
        // 播放位置早于第一行
        return -1;
    }
    --it;
    return static_cast<int>(std::distance(lines_.begin(), it));
}

auto LyricsManager::GetLines() const -> std::vector<LrcLine> {
    const std::lock_guard<std::mutex> lock(mutex_);
    return lines_;
}

auto LyricsManager::GetState() const -> LyricsState {
    const std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

void LyricsManager::Clear() {
    const std::lock_guard<std::mutex> lock(mutex_);
    lines_.clear();
    state_ = LyricsState::kIdle;
    ++generation_;  // 令正在进行的后台加载失效
}

}  // namespace gemusic::lyrics
