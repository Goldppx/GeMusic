#ifndef GEMUSIC_PLAYER_MUSIC_PLAYER_H
#define GEMUSIC_PLAYER_MUSIC_PLAYER_H

#include <cstdint>

#include <expected>
#include <functional>
#include <memory>
#include <string>

#include "gemusic/error.h"

namespace gemusic::player {

// 播放器状态枚举
enum class PlayerState {
    kStopped,   // 已停止
    kPlaying,   // 正在播放
    kPaused,    // 已暂停
    kLoading,   // 加载中
};

// 当前播放曲目的信息
struct TrackInfo {
    std::string title;       // 曲目标题
    std::string artist;      // 艺术家
    std::string album;       // 专辑名
    uint32_t duration_ms;    // 总时长（毫秒）
    uint32_t position_ms;    // 当前播放位置（毫秒）
};

// 音乐播放器，封装 miniaudio 进行音频解码和播放
class MusicPlayer {
public:
    // 播放状态变化回调类型
    using StateCallback = std::function<void(PlayerState)>;

    MusicPlayer();
    ~MusicPlayer();

    // 禁止拷贝，允许移动
    MusicPlayer(const MusicPlayer&) = delete;
    auto operator=(const MusicPlayer&) -> MusicPlayer& = delete;
    MusicPlayer(MusicPlayer&&) noexcept;
    auto operator=(MusicPlayer&&) noexcept -> MusicPlayer&;

    // 加载并播放音频文件或 URL
    // 参数: source - 本地文件路径或远程 URL
    auto Play(std::string_view source) -> std::expected<void, AppError>;

    // 暂停播放
    void Pause();

    // 恢复播放
    void Resume();

    // 停止播放
    void Stop();

    // 设置音量
    // 参数: volume - 音量值（0-100）
    void SetVolume(int volume);

    // 跳转到指定位置
    // 参数: position_ms - 目标位置（毫秒）
    auto Seek(uint32_t position_ms) -> std::expected<void, AppError>;

    // 获取当前播放状态
    auto GetState() const -> PlayerState;

    // 获取当前曲目信息
    auto GetTrackInfo() const -> const TrackInfo&;

    // 注册状态变化回调
    void OnStateChanged(StateCallback callback);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace gemusic::player

#endif  // GEMUSIC_PLAYER_MUSIC_PLAYER_H
