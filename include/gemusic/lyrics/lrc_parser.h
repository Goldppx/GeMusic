#ifndef GEMUSIC_LYRICS_LRC_PARSER_H
#define GEMUSIC_LYRICS_LRC_PARSER_H

#include <cstdint>
#include <string>
#include <vector>

namespace gemusic::lyrics {

// LRC 歌词的一行：时间戳（毫秒）+ 文本内容
struct LrcLine {
    // 该行歌词对应的播放时间点（毫秒）
    uint32_t time_ms = 0;

    // 该行歌词文本（已去除时间标签）
    std::string text;
};

// 解析 LRC 格式歌词文本，返回按时间升序排列的歌词行列表
// 支持标准 [mm:ss.xx] / [mm:ss.xxx] / [mm:ss] 格式
// 忽略元数据标签（[ti:], [ar:], [al:] 等）及空行
// 参数: lrc_text - 完整的 LRC 格式文本（允许 \r\n 或 \n 换行）
// 返回: 按 time_ms 升序排序的 LrcLine 列表（可能为空）
auto ParseLrc(const std::string& lrc_text) -> std::vector<LrcLine>;

}  // namespace gemusic::lyrics

#endif  // GEMUSIC_LYRICS_LRC_PARSER_H
