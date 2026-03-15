#ifndef GEMUSIC_NETWORK_NETEASE_API_H
#define GEMUSIC_NETWORK_NETEASE_API_H

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

#include "gemusic/error.h"
#include "gemusic/network/api_client.h"

namespace gemusic::network {

// 代表一个网易云歌单的数据结构
struct Playlist {
    // 歌单数字 ID
    int64_t id = 0;

    // 歌单名称
    std::string name;

    // 歌单内歌曲数量
    int track_count = 0;

    // 播放次数
    int64_t play_count = 0;

    // 封面图片 URL（用于后续图片加载，当前仅保存）
    std::string cover_img_url;

    // true 表示用户自建歌单，false 表示用户收藏的他人歌单
    bool is_owned = false;
};

// 获取指定用户的歌单列表（需已登录，api_client 中含有效 cookies）
// 参数:
//   client - 已配置 cookies 的 HTTP 客户端
//   uid    - 目标用户的网易云数字 ID（即 settings.user_id）
// 返回: 成功时返回歌单列表，失败时返回 AppError
auto FetchUserPlaylists(ApiClient& client, int64_t uid)
    -> std::expected<std::vector<Playlist>, AppError>;

// 代表歌单中一首歌曲的数据结构
struct Track {
    // 歌曲数字 ID
    int64_t id = 0;

    // 歌曲名称
    std::string name;

    // 主要艺术家名（多艺术家时取第一个）
    std::string artist;

    // 所属专辑名
    std::string album;

    // 歌曲时长（毫秒）
    int duration_ms = 0;
};

// 获取指定歌单的所有曲目（需已登录）
// 参数:
//   client      - 已配置 cookies 的 HTTP 客户端
//   playlist_id - 歌单数字 ID
// 返回: 成功时返回曲目列表，失败时返回 AppError
auto FetchPlaylistTracks(ApiClient& client, int64_t playlist_id)
    -> std::expected<std::vector<Track>, AppError>;

// 获取指定歌曲的可播放 URL（需已登录）
// 若歌曲因版权或会员限制不可用，返回空字符串（非错误）
// 参数:
//   client  - 已配置 cookies 的 HTTP 客户端
//   song_id - 歌曲数字 ID
// 返回: 成功时返回播放 URL 或空字符串（不可用），失败时返回 AppError
auto FetchSongUrl(ApiClient& client, int64_t song_id) -> std::expected<std::string, AppError>;

// 获取指定歌曲的 LRC 格式歌词文本（需已登录）
// 若歌曲无歌词，返回空字符串（非错误）
// 参数:
//   client  - 已配置 cookies 的 HTTP 客户端
//   song_id - 歌曲数字 ID
// 返回: 成功时返回 LRC 文本或空字符串（无歌词），失败时返回 AppError
auto FetchSongLyrics(ApiClient& client, int64_t song_id) -> std::expected<std::string, AppError>;

// 搜索结果中单首歌曲的数据结构（与 Track 相同字段，语义上用于搜索结果展示）
struct SearchResult {
    // 歌曲数字 ID
    int64_t id = 0;

    // 歌曲名称
    std::string name;

    // 主要艺术家名（多艺术家时取第一个）
    std::string artist;

    // 所属专辑名
    std::string album;

    // 歌曲时长（毫秒）
    int duration_ms = 0;
};

// 在网易云音乐搜索歌曲（需已登录）
// 使用 /weapi/cloudsearch/get/web 端点，type=1 表示单曲搜索
// 参数:
//   client  - 已配置 cookies 的 HTTP 客户端
//   keyword - 搜索关键词（支持歌名、艺术家、专辑名等）
//   limit   - 最多返回结果数量（默认 30）
//   offset  - 分页偏移（默认 0）
// 返回: 成功时返回搜索结果列表，失败时返回 AppError
auto FetchSearchResults(ApiClient& client, const std::string& keyword, int limit = 30,
                        int offset = 0) -> std::expected<std::vector<SearchResult>, AppError>;

}  // namespace gemusic::network

#endif  // GEMUSIC_NETWORK_NETEASE_API_H
