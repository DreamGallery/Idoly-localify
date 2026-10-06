#pragma once
#include <string>
#include <string_view>

namespace guild_medal_font {
inline constexpr std::string_view path = "WindowSystem/WindowRoot/LiveWindow(Clone)/GuildWindow(Clone)/GuildTopScreen(Clone)/ScreenCanvas/ScreenRoot/UnionMedal/Amount";
// Return no edit for other content or an already decorated value. Keep the
// numeric bytes intact; rich-text font selection affects only their rendering.
inline std::string decorate(std::string_view text, std::string_view originalFont) {
    constexpr std::string_view prefix = "联盟奖牌：";
    constexpr std::string_view japanese = "ユニオンメダル：";
    const auto length = text.substr(0, prefix.size()) == prefix ? prefix.size() :
        text.substr(0, japanese.size()) == japanese ? japanese.size() : 0;
    if (!length || originalFont.empty() || originalFont.size() > 128)
        return {};
    for (unsigned char c : originalFont)
        if (c < 32 || c == '"' || c == '\'' || c == '<' || c == '>') return {};
    const auto number = text.substr(length);
    if (number.empty() || number.size() > 32) return {};
    size_t group = 0;
    bool comma = false;
    for (char c : number) {
        if (c >= '0' && c <= '9') { ++group; continue; }
        if (c != ',' || group == 0 || (comma ? group != 3 : group > 3)) return {};
        comma = true; group = 0;
    }
    if (group == 0 || (comma && group != 3)) return {};
    return std::string(prefix) + "<font=\"" + std::string(originalFont) + "\">" +
        std::string(number) + "</font>";
}
} // namespace guild_medal_font
