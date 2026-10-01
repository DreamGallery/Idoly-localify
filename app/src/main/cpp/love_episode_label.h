#pragma once
#include <optional>
#include <string>
#include <string_view>

namespace love_episode_label {
inline constexpr std::string_view path = "WindowSystem/WindowRoot/LoveWindow(Clone)/LoveHomeScreen(Clone)/ScreenCanvas/ScreenRoot/LowerRoot/LoveStartButton/NotiRoot/PlayingStoryNoti/TextRoot/EpisodeText";
inline bool isNumber(std::string_view value) {
    if (value.empty() || value.size() > 4) return false;
    for (const char c : value) if (c < '0' || c > '9') return false;
    return true;
}
inline std::optional<std::string> format(std::string_view objectPath, std::string_view value) {
    if (objectPath != path) return std::nullopt;
    if (value == "N") return std::string(); // Prefab placeholder, before game data arrives.
    if (isNumber(value)) return "第" + std::string(value) + "话进行中";
    constexpr std::string_view prefix = "第", suffix = "话进行中";
    if (value.size() > prefix.size() + suffix.size() &&
        value.substr(0, prefix.size()) == prefix &&
        value.substr(value.size() - suffix.size()) == suffix &&
        isNumber(value.substr(prefix.size(), value.size() - prefix.size() - suffix.size())))
        return std::string(value);
    return std::nullopt;
}

// Formatted setters can replace TMP's processing buffer while its string
// getter still contains our previous label. Read the digits being rendered.
template <typename ReadCodepoint>
inline std::optional<std::string> numberBuffer(size_t capacity, ReadCodepoint read) {
    std::string digits;
    for (size_t i = 0; i < capacity && i <= 4; ++i) {
        const auto cp = read(i);
        if (cp == 0) return isNumber(digits) ? std::optional<std::string>(digits) : std::nullopt;
        if (i == 4 || cp < '0' || cp > '9') return std::nullopt;
        digits.push_back(static_cast<char>(cp));
    }
    return std::nullopt;
}
}
