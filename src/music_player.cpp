#include "gemusic/player/music_player.h"

#include <iostream>
#include <mutex>

// miniaudio 实现（只在一个 .cpp 中定义）
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

namespace gemusic::player {

// Pimpl 实现
struct MusicPlayer::Impl {
    ma_engine engine{};
    ma_sound sound{};
    bool engine_initialized = false;
    bool sound_loaded = false;
    PlayerState state = PlayerState::kStopped;
    TrackInfo track_info{};
    StateCallback state_callback;
    std::mutex mutex;

    Impl() {
        // 初始化 miniaudio 引擎
        auto result = ma_engine_init(nullptr, &engine);
        if (result != MA_SUCCESS) {
            std::cerr << "miniaudio 引擎初始化失败，错误码: " << result << std::endl;
            return;
        }
        engine_initialized = true;
    }

    ~Impl() {
        if (sound_loaded) {
            ma_sound_uninit(&sound);
        }
        if (engine_initialized) {
            ma_engine_uninit(&engine);
        }
    }

    // 更新播放状态并触发回调
    void SetState(PlayerState new_state) {
        state = new_state;
        if (state_callback) {
            state_callback(new_state);
        }
    }
};

MusicPlayer::MusicPlayer() : impl_(std::make_unique<Impl>()) {}

MusicPlayer::~MusicPlayer() = default;
MusicPlayer::MusicPlayer(MusicPlayer&&) noexcept = default;
auto MusicPlayer::operator=(MusicPlayer&&) noexcept -> MusicPlayer& = default;

auto MusicPlayer::Play(std::string_view source) -> std::expected<void, AppError> {
    std::lock_guard lock(impl_->mutex);

    if (!impl_->engine_initialized) {
        return std::unexpected(AppError{ErrorCode::kPlayerError, "音频引擎未初始化"});
    }

    // 如果之前有加载的音频，先卸载
    if (impl_->sound_loaded) {
        ma_sound_uninit(&impl_->sound);
        impl_->sound_loaded = false;
    }

    impl_->SetState(PlayerState::kLoading);

    // 加载音频源
    const auto result = ma_sound_init_from_file(&impl_->engine, std::string(source).c_str(), 0,
                                                nullptr, nullptr, &impl_->sound);

    if (result != MA_SUCCESS) {
        impl_->SetState(PlayerState::kStopped);
        return std::unexpected(
            AppError{ErrorCode::kPlayerError, std::string("加载音频失败: ") + std::string(source)});
    }

    impl_->sound_loaded = true;

    // 开始播放
    ma_sound_start(&impl_->sound);
    impl_->SetState(PlayerState::kPlaying);

    // 更新曲目信息
    impl_->track_info.title = std::string(source);

    float length = 0;
    ma_sound_get_length_in_seconds(&impl_->sound, &length);
    impl_->track_info.duration_ms = static_cast<uint32_t>(length * 1000.0F);

    return {};
}

void MusicPlayer::Pause() {
    std::lock_guard lock(impl_->mutex);
    if (impl_->sound_loaded && impl_->state == PlayerState::kPlaying) {
        ma_sound_stop(&impl_->sound);
        impl_->SetState(PlayerState::kPaused);
    }
}

void MusicPlayer::Resume() {
    std::lock_guard lock(impl_->mutex);
    if (impl_->sound_loaded && impl_->state == PlayerState::kPaused) {
        ma_sound_start(&impl_->sound);
        impl_->SetState(PlayerState::kPlaying);
    }
}

void MusicPlayer::Stop() {
    std::lock_guard lock(impl_->mutex);
    if (impl_->sound_loaded) {
        ma_sound_stop(&impl_->sound);
        ma_sound_seek_to_pcm_frame(&impl_->sound, 0);
        impl_->SetState(PlayerState::kStopped);
    }
}

void MusicPlayer::SetVolume(int volume) {
    std::lock_guard lock(impl_->mutex);
    // 将 0-100 映射到 0.0-1.0
    const float vol = static_cast<float>(std::clamp(volume, 0, 100)) / 100.0F;
    ma_engine_set_volume(&impl_->engine, vol);
}

auto MusicPlayer::Seek(uint32_t position_ms) -> std::expected<void, AppError> {
    std::lock_guard lock(impl_->mutex);
    if (!impl_->sound_loaded) {
        return std::unexpected(AppError{ErrorCode::kPlayerError, "没有正在播放的音频"});
    }

    // 将毫秒转换为采样帧
    const auto sample_rate = ma_engine_get_sample_rate(&impl_->engine);
    const auto frame = static_cast<ma_uint64>(position_ms) * sample_rate / 1000;
    ma_sound_seek_to_pcm_frame(&impl_->sound, frame);

    return {};
}

auto MusicPlayer::GetState() const -> PlayerState {
    return impl_->state;
}

auto MusicPlayer::GetTrackInfo() const -> const TrackInfo& {
    // 每次调用时从 miniaudio 读取最新播放位置，保证进度条数据实时
    // impl_ 是 unique_ptr<Impl>，const 方法中指针本身不可变，但指向的 Impl 对象可修改
    if (impl_->sound_loaded && impl_->state != PlayerState::kStopped) {
        float cursor = 0.0F;
        ma_sound_get_cursor_in_seconds(&impl_->sound, &cursor);
        impl_->track_info.position_ms = static_cast<uint32_t>(cursor * 1000.0F);
    }
    return impl_->track_info;
}

void MusicPlayer::OnStateChanged(StateCallback callback) {
    impl_->state_callback = std::move(callback);
}

}  // namespace gemusic::player
