#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Only reviewed home work labels and in-work footer labels use these short names.
// This does not weaken NumericTextTemplateMatcher's global literal guard.
class HomeActivityNameMatcher {
public:
    bool add(const std::string& source, const std::string& target) {
        if (names.size() >= 64 || !validName(source) || !validName(target)) return false;
        for (const auto& item : names) {
            if (item.first == source) return false;
            if ((target == item.first && source != target && item.first != item.second) ||
                (item.second == source && item.first != item.second && source != target)) return false;
        }
        names.emplace_back(source, target);
        return true;
    }

    // Cheap text-only filter before asking Unity for an object's parent chain.
    bool mayMatch(std::string_view text) const {
        std::string_view name, level;
        if (!splitLabel(text, name, level)) return false;
        for (const auto& item : names)
            if (name == item.first || name == item.second) return true;
        return false;
    }

    std::string translate(std::string_view text, std::string_view path) const {
        if (!isTargetPath(path)) return {};
        std::string_view name, level;
        if (!splitLabel(text, name, level)) return {};
        for (const auto& item : names) {
            if (name != item.first) continue;
            std::string result = item.second;
            result += " Lv";
            result.append(level);
            return result;
        }
        return {};
    }

    bool isRendered(std::string_view text, std::string_view path) const {
        if (!isTargetPath(path)) return false;
        std::string_view name, level;
        if (!splitLabel(text, name, level)) return false;
        for (const auto& item : names)
            if (name == item.second) return true;
        return false;
    }

    std::size_t size() const { return names.size(); }

    bool mayMatchDuration(std::string_view text, std::string_view pattern) const {
        std::string_view name;
        if (!durationName(text, pattern, name)) return false;
        for (const auto& item : names)
            if (name == item.first || name == item.second) return true;
        return false;
    }

    std::string translateDuration(std::string_view text, std::string_view path,
                                  std::string_view pattern) const {
        constexpr std::string_view target =
            "WindowSystem/WindowRoot/ActivityWindow(Clone)/FanEventCharacterScreen(Clone)/"
            "ScreenCanvas/ScreenRoot/ActivityCharacterSelectCommonUI/Fullscreen/"
            "ActivityCharacterSelectCommonUpper/FadeRoot/RequiredTime/RequiredTimeTitle";
        if (path != target) return {};
        std::string_view name;
        if (!durationName(text, pattern, name)) return {};
        for (const auto& item : names) {
            if (name != item.first) continue;
            auto result = std::string(pattern);
            result.replace(result.find("{0}"), 3, item.second);
            return result;
        }
        return {};
    }

    static bool isTargetPath(std::string_view path) {
        constexpr std::string_view workPrefix = "WindowSystem/WindowRoot/ActivityWindow(Clone)/";
        constexpr std::string_view workSuffix = "/ScreenCanvas/ScreenRoot/ActGameFooter/ActGameInfo/Level";
        if (path.substr(0, workPrefix.size()) == workPrefix) {
            const auto rest = path.substr(workPrefix.size());
            for (const auto screen : {std::string_view("FanEventScreen(Clone)"),
                std::string_view("PromotionScreen(Clone)"), std::string_view("RefreshScreen(Clone)")}) {
                if (rest.substr(0, screen.size()) == screen && rest.substr(screen.size()) == workSuffix) return true;
            }
        }
        constexpr std::string_view prefix =
            "WindowSystem/WindowRoot/HomeWindow(Clone)/HomeTopScreen(Clone)/"
            "ScreenCanvas/ScreenRoot/ActivityPage/SlideRoot/Parent/ActivityCells/";
        if (path.substr(0, prefix.size()) != prefix) return false;
        const auto remaining = path.substr(prefix.size());
        return remaining == "FanEvent/NameBase/Name" ||
               remaining == "Promotion/NameBase/Name" ||
               remaining == "Refresh/NameBase/Name";
    }

private:
    static bool durationName(std::string_view text, std::string_view pattern,
                             std::string_view& name) {
        const auto slot = pattern.find("{0}");
        if (slot == std::string_view::npos || pattern.find("{", slot + 3) != std::string_view::npos ||
            pattern.substr(0, slot).find_first_of("{}") != std::string_view::npos ||
            pattern.substr(slot + 3).find('}') != std::string_view::npos) return false;
        const auto prefix = pattern.substr(0, slot), suffix = pattern.substr(slot + 3);
        if (prefix.empty() && suffix.empty()) return false;
        if (text.size() <= prefix.size() + suffix.size() ||
            text.substr(0, prefix.size()) != prefix ||
            text.substr(text.size() - suffix.size()) != suffix) return false;
        name = text.substr(prefix.size(), text.size() - prefix.size() - suffix.size());
        return validName(name);
    }

    static bool validName(std::string_view name) {
        if (name.empty() || name.size() > 64) return false;
        for (unsigned char c : name)
            if (c <= 32 || c == 127 || c == '<' || c == '>' || c == '{' || c == '}') return false;
        return true;
    }

    static bool splitLabel(std::string_view text, std::string_view& name,
                           std::string_view& level) {
        const auto split = text.rfind(" Lv");
        if (split == std::string_view::npos || split == 0) return false;
        name = text.substr(0, split);
        level = text.substr(split + 3);
        if (level.empty() || level.size() > 6) return false;
        for (char c : level)
            if (c < '0' || c > '9') return false;
        return true;
    }

    std::vector<std::pair<std::string, std::string>> names;
};
