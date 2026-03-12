#include "gemusic/network/netease_api.h"

#include <iostream>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

namespace gemusic::network {

// 获取用户歌单列表的 Weapi 端点（直连，无需代理）
static constexpr std::string_view kUserPlaylistEndpoint =
    "https://music.163.com/weapi/user/playlist";

// 单次请求返回的最大歌单数量（网易云 API 最多支持 1000）
static constexpr int kPlaylistLimit = 1000;

auto FetchUserPlaylists(ApiClient& client, int64_t uid)
    -> std::expected<std::vector<Playlist>, AppError> {
    // uid 为 0 表示未登录或 user_id 尚未解析，提前返回错误
    if (uid == 0) {
        return std::unexpected(
            AppError{ErrorCode::kAuthFailed, "user_id 为 0，请先登录后再获取歌单"});
    }

    // 构造请求参数：uid 为目标用户 ID，includeVideo 过滤掉视频歌单
    const nlohmann::json params = {
        {"uid", uid},
        {"limit", kPlaylistLimit},
        {"offset", 0},
        {"includeVideo", true},
    };

    spdlog::debug("FetchUserPlaylists: 请求用户 {} 的歌单列表", uid);

    auto result = client.PostWeapi(kUserPlaylistEndpoint, params);
    if (!result) {
        spdlog::warn("FetchUserPlaylists: HTTP 请求失败: {}", result.error().message);
        return std::unexpected(result.error());
    }

    const auto& resp = result.value();

    // 校验业务响应码
    if (!resp.json.is_object() || !resp.json.contains("code") ||
        resp.json["code"].get<int>() != 200) {
        const int code = resp.json.is_object() ? resp.json.value("code", -1) : -1;
        spdlog::warn("FetchUserPlaylists: 服务器返回异常 code={}", code);
        return std::unexpected(AppError{ErrorCode::kNetworkError,
                                        "获取歌单失败，服务器 code=" + std::to_string(code)});
    }

    // 解析歌单数组
    if (!resp.json.contains("playlist") || !resp.json["playlist"].is_array()) {
        return std::unexpected(
            AppError{ErrorCode::kParseError, "响应中缺少 playlist 字段或格式错误"});
    }

    std::vector<Playlist> playlists;
    const auto& arr = resp.json["playlist"];
    playlists.reserve(arr.size());

    for (const auto& item : arr) {
        if (!item.is_object()) {
            continue;
        }

        Playlist pl;
        pl.id = item.value("id", int64_t{0});
        pl.name = item.value("name", std::string{});
        pl.track_count = item.value("trackCount", 0);
        pl.play_count = item.value("playCount", int64_t{0});
        pl.cover_img_url = item.value("coverImgUrl", std::string{});

        // 判断是否为自建歌单：歌单创建者 ID 与当前用户 ID 相同
        const int64_t creator_id = item.value("userId", int64_t{0});
        pl.is_owned = (creator_id == uid);

        playlists.push_back(std::move(pl));
    }

    spdlog::info("FetchUserPlaylists: 获取到 {} 个歌单", playlists.size());
    return playlists;
}

// 获取歌单详情的 Weapi 端点
static constexpr std::string_view kPlaylistDetailEndpoint =
    "https://music.163.com/weapi/v6/playlist/detail";

// 获取歌曲播放 URL 的 Weapi 端点
static constexpr std::string_view kSongUrlEndpoint =
    "https://music.163.com/weapi/song/enhance/player/url/v1";

auto FetchPlaylistTracks(ApiClient& client, int64_t playlist_id)
    -> std::expected<std::vector<Track>, AppError> {
    // 请求参数：id 为歌单 ID，n 取尽量多的曲目，s 为最近听过的歌曲数
    const nlohmann::json params = {
        {"id", playlist_id},
        {"n", 100000},
        {"s", 8},
    };

    spdlog::debug("FetchPlaylistTracks: 请求歌单 {} 的曲目列表", playlist_id);

    auto result = client.PostWeapi(kPlaylistDetailEndpoint, params);
    if (!result) {
        spdlog::warn("FetchPlaylistTracks: HTTP 请求失败: {}", result.error().message);
        return std::unexpected(result.error());
    }

    const auto& resp = result.value();

    // 校验业务响应码
    if (!resp.json.is_object() || !resp.json.contains("code") ||
        resp.json["code"].get<int>() != 200) {
        const int code = resp.json.is_object() ? resp.json.value("code", -1) : -1;
        spdlog::warn("FetchPlaylistTracks: 服务器返回异常 code={}", code);
        return std::unexpected(AppError{ErrorCode::kNetworkError,
                                        "获取曲目失败，服务器 code=" + std::to_string(code)});
    }

    // 解析 playlist.tracks 数组
    if (!resp.json.contains("playlist") || !resp.json["playlist"].is_object()) {
        return std::unexpected(
            AppError{ErrorCode::kParseError, "响应中缺少 playlist 字段或格式错误"});
    }

    const auto& pl_obj = resp.json["playlist"];
    if (!pl_obj.contains("tracks") || !pl_obj["tracks"].is_array()) {
        return std::unexpected(
            AppError{ErrorCode::kParseError, "响应中缺少 tracks 字段或格式错误"});
    }

    std::vector<Track> tracks;
    const auto& arr = pl_obj["tracks"];
    tracks.reserve(arr.size());

    for (const auto& item : arr) {
        if (!item.is_object()) {
            continue;
        }

        Track tr;
        tr.id = item.value("id", int64_t{0});
        tr.name = item.value("name", std::string{});
        tr.duration_ms = item.value("dt", 0);

        // 取第一个艺术家名
        if (item.contains("ar") && item["ar"].is_array() && !item["ar"].empty()) {
            tr.artist = item["ar"][0].value("name", std::string{});
        }

        // 专辑名
        if (item.contains("al") && item["al"].is_object()) {
            tr.album = item["al"].value("name", std::string{});
        }

        tracks.push_back(std::move(tr));
    }

    spdlog::info("FetchPlaylistTracks: 获取到 {} 首曲目", tracks.size());
    return tracks;
}

auto FetchSongUrl(ApiClient& client, int64_t song_id) -> std::expected<std::string, AppError> {
    // ids 字段须为字符串形式的 JSON 数组（网易云 weapi 特殊要求），
    // 例如 "[1234567]"，而非真正的 JSON 数组 [1234567]
    // 参考：go-musicfox/netease-music/service/song_url_v1_service.go
    const nlohmann::json params = {
        {"ids", "[" + std::to_string(song_id) + "]"},
        {"level", "standard"},
        {"encodeType", "mp3"},
    };

    spdlog::debug("FetchSongUrl: 请求歌曲 {} 的播放地址", song_id);

    auto result = client.PostWeapi(kSongUrlEndpoint, params);
    if (!result) {
        spdlog::warn("FetchSongUrl: HTTP 请求失败: {}", result.error().message);
        return std::unexpected(result.error());
    }

    const auto& resp = result.value();

    // 校验业务响应码
    if (!resp.json.is_object() || !resp.json.contains("code") ||
        resp.json["code"].get<int>() != 200) {
        const int code = resp.json.is_object() ? resp.json.value("code", -1) : -1;
        spdlog::warn("FetchSongUrl: 服务器返回异常 code={}", code);
        return std::unexpected(AppError{ErrorCode::kNetworkError,
                                        "获取播放地址失败，服务器 code=" + std::to_string(code)});
    }

    // data 为数组，取 data[0].url；url 为 JSON null 时表示歌曲不可用（非错误）
    if (!resp.json.contains("data") || !resp.json["data"].is_array() || resp.json["data"].empty()) {
        return std::unexpected(AppError{ErrorCode::kParseError, "响应中缺少 data 字段或格式错误"});
    }

    const auto& url_obj = resp.json["data"][0];
    if (!url_obj.is_object()) {
        return std::unexpected(AppError{ErrorCode::kParseError, "data[0] 格式错误"});
    }

    // url 字段可能为 JSON null（版权限制），此时返回空字符串
    if (!url_obj.contains("url") || url_obj["url"].is_null()) {
        spdlog::info("FetchSongUrl: 歌曲 {} 暂无版权，url 为 null", song_id);
        return std::string{};
    }

    return url_obj["url"].get<std::string>();
}

}  // namespace gemusic::network
