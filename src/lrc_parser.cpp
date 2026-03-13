#include "gemusic/lyrics/lrc_parser.h"

#include <algorithm>
#include <charconv>
#include <sstream>
#include <string_view>

namespace gemusic::lyrics {

namespace {

// 解析单个时间标签 "[mm:ss.xx]" / "[mm:ss.xxx]" / "[mm:ss]"
// 成功时将毫秒数写入 out_ms 并返回 true，格式不符时返回 false
auto ParseTimeTag(std::string_view tag, uint32_t& out_ms) -> bool {
    // tag 形如 "[02:34.56]"，需去掉首尾括号
    if (tag.size() < 7 || tag.front() != '[' || tag.back() != ']') {
        return false;
    }
    // 去掉 '[' 和 ']'
    const std::string_view inner = tag.substr(1, tag.size() - 2);

    // 查找 ':'（分钟与秒的分隔符）
    const auto colon_pos = inner.find(':');
    if (colon_pos == std::string_view::npos) {
        return false;
    }

    // 解析分钟数
    uint32_t minutes = 0;
    const auto min_str = inner.substr(0, colon_pos);
    const auto [min_end, min_ec] =
        std::from_chars(min_str.data(), min_str.data() + min_str.size(), minutes);
    if (min_ec != std::errc{}) {
        return false;
    }

    // 冒号之后的剩余部分（秒 + 可选小数）
    const std::string_view rest = inner.substr(colon_pos + 1);

    // 查找小数点（允许不存在）
    const auto dot_pos = rest.find('.');
    uint32_t seconds = 0;
    uint32_t frac_ms = 0;

    // 解析整数秒
    const std::string_view sec_str =
        (dot_pos != std::string_view::npos) ? rest.substr(0, dot_pos) : rest;
    const auto [sec_end, sec_ec] =
        std::from_chars(sec_str.data(), sec_str.data() + sec_str.size(), seconds);
    if (sec_ec != std::errc{}) {
        return false;
    }

    // 解析小数部分（2 位 = 百分之秒，3 位 = 毫秒）
    if (dot_pos != std::string_view::npos) {
        const std::string_view frac = rest.substr(dot_pos + 1);
        // 只取前 3 位
        const std::string_view frac_trunc = frac.substr(0, std::min(frac.size(), size_t{3}));
        uint32_t frac_val = 0;
        const auto [frac_end, frac_ec] =
            std::from_chars(frac_trunc.data(), frac_trunc.data() + frac_trunc.size(), frac_val);
        if (frac_ec == std::errc{}) {
            // 根据位数换算为毫秒
            if (frac_trunc.size() == 1) {
                frac_ms = frac_val * 100;  // 1 位：tenths → ms
            } else if (frac_trunc.size() == 2) {
                frac_ms = frac_val * 10;  // 2 位：hundredths → ms
            } else {
                frac_ms = frac_val;  // 3 位：直接为 ms
            }
        }
    }

    out_ms = minutes * 60'000 + seconds * 1'000 + frac_ms;
    return true;
}

}  // namespace

auto ParseLrc(const std::string& lrc_text) -> std::vector<LrcLine> {
    std::vector<LrcLine> result;

    std::istringstream stream(lrc_text);
    std::string raw_line;

    while (std::getline(stream, raw_line)) {
        // 去除行尾 \r（Windows 换行符）
        if (!raw_line.empty() && raw_line.back() == '\r') {
            raw_line.pop_back();
        }

        // 跳过空行
        if (raw_line.empty()) {
            continue;
        }

        std::string_view line = raw_line;

        // 收集当前行前缀中的所有时间标签（一行可含多个时间标签，如 "[00:10.00][01:20.00]text"）
        std::vector<uint32_t> time_stamps;

        while (!line.empty() && line.front() == '[') {
            // 找到匹配的 ']'
            const auto close = line.find(']');
            if (close == std::string_view::npos) {
                break;
            }
            const std::string_view tag = line.substr(0, close + 1);
            uint32_t ms = 0;
            if (ParseTimeTag(tag, ms)) {
                time_stamps.push_back(ms);
                line = line.substr(close + 1);
            } else {
                // 元数据标签（如 [ti:xxx]），跳过整行
                break;
            }
        }

        // 若未解析到任何时间标签，跳过此行
        if (time_stamps.empty()) {
            continue;
        }

        // 剩余部分为歌词文本（可能为空，表示空白行歌词）
        const std::string text{line};

        // 为每个时间戳生成一条歌词行
        for (const uint32_t ts : time_stamps) {
            result.push_back({ts, text});
        }
    }

    // 按时间升序排列（多时间标签情况下可能乱序）
    std::sort(result.begin(), result.end(),
              [](const LrcLine& a, const LrcLine& b) { return a.time_ms < b.time_ms; });

    return result;
}

}  // namespace gemusic::lyrics
