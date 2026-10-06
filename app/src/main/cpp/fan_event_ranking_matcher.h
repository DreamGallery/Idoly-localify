#pragma once
#include <array>
#include <string>
#include <string_view>

namespace fan_event_ranking {
inline constexpr std::string_view pathSuffix =
    "FanEventRankingSheet(Clone)/Canvas/Root/UIContentArea/SheetContainer/Header/Description";
inline constexpr const char* names[] = {"握手会", "撮影会", "サイン会"};

template <typename Dictionary>
bool mayMatch(std::string_view value, const Dictionary& dictionary) {
    const auto name = value.substr(0, value.find(' '));
    for (const auto* source : names) {
        const auto found = dictionary.find(source);
        if (name == source || (found != dictionary.end() && name == found->second)) return true;
    }
    return false;
}

template <size_t N>
bool splitPattern(std::string_view pattern, std::array<std::string_view, N + 1>& parts) {
    if (pattern.empty() || pattern.size() > 256) return false;
    size_t start = 0;
    for (size_t i = 0; i < N; ++i) {
        const auto token = "{" + std::to_string(i) + "}";
        const auto pos = pattern.find(token, start);
        if (pos == std::string_view::npos) return false;
        parts[i] = pattern.substr(start, pos - start);
        start = pos + token.size();
    }
    parts[N] = pattern.substr(start);
    for (auto part : parts) if (part.find_first_of("{}\r\n") != std::string_view::npos) return false;
    return true;
}

inline bool digits(std::string_view value) {
    if (value.empty() || value.size() > 6) return false;
    for (char c : value) if (c < '0' || c > '9') return false;
    return true;
}

// The I18n sentence is translated before the game inserts its activity name.
// Match this prefab's header only, using the current translated sentence and
// group patterns. Both numeric arguments and their leading zeroes stay intact.
template <typename Dictionary>
std::string translate(std::string_view value, std::string_view path, const Dictionary& dictionary,
                      std::string_view rankingPattern, std::string_view groupPattern) {
    if (path.size() < pathSuffix.size()) return {};
    const auto prefixSize = path.size() - pathSuffix.size();
    if (path.substr(prefixSize) != pathSuffix || (prefixSize && path[prefixSize - 1] != '/')) return {};
    std::array<std::string_view, 4> rank;
    std::array<std::string_view, 2> group;
    if (!splitPattern<3>(rankingPattern, rank) || !rank[0].empty() || rank[1].empty() ||
        !splitPattern<1>(groupPattern, group) || group[0].empty()) return {};
    const auto middle = std::string(rank[2]) + std::string(group[0]);
    const auto suffix = std::string(group[1]) + std::string(rank[3]);
    if (middle.empty() || suffix.empty() || value.size() > 256) return {};
    for (const auto* source : names) {
        const auto found = dictionary.find(source);
        if (found == dictionary.end() || found->second.empty()) continue;
        for (const std::string_view name : {std::string_view(source), std::string_view(found->second)}) {
            const auto prefix = std::string(name) + std::string(rank[1]);
            if (value.size() < prefix.size() + middle.size() + suffix.size() + 2 ||
                value.substr(0, prefix.size()) != prefix || value.substr(value.size() - suffix.size()) != suffix) continue;
            const auto boundary = value.find(middle, prefix.size());
            if (boundary == std::string_view::npos) continue;
            const auto count = value.substr(prefix.size(), boundary - prefix.size());
            const auto groupStart = boundary + middle.size();
            if (groupStart > value.size() - suffix.size()) continue;
            if (!digits(count) || !digits(value.substr(groupStart, value.size() - suffix.size() - groupStart))) continue;
            return found->second + std::string(value.substr(name.size()));
        }
    }
    return {};
}
} // namespace fan_event_ranking
