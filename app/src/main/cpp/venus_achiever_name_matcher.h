#pragma once
#include <string>

// The observed ranking cell formats the name before assigning TMP text. Match
// only that cell and retain the opaque player name without dictionary entries.
class VenusAchieverNameMatcher {
public:
    static bool mayMatch(const std::string& text) {
        return prefixLength(text) != 0;
    }
    static std::string translate(const std::string& text, const std::string& path,
                                 const std::string& translatedTemplate) {
        constexpr const char* target = "WindowSystem/WindowRoot/VenusWindow(Clone)/VenusAchieverScreen(Clone)/ScreenCanvas/ScreenRoot/VenusAchieverList/Scroll/Viewport/Content/VenusAchieverListCell(Clone)/Cell/FirstAchieverName";
        const auto prefix = prefixLength(text);
        if (!prefix || path != target) return {};
        const auto name = text.substr(prefix);
        if (name.empty() || name.size() > 256 || name == "{0}" ||
            name.find_first_of("\r\n") != std::string::npos || name.find('\0') != std::string::npos)
            return {};
        const auto token = translatedTemplate.find("{0}");
        if (token == std::string::npos || translatedTemplate.find("{0}", token + 3) != std::string::npos)
            return {};
        return translatedTemplate.substr(0, token) + name + translatedTemplate.substr(token + 3);
    }
private:
    static size_t prefixLength(const std::string& text) {
        // Full-width form was captured in the cell; ASCII form is the game's
        // venus.firstAchiever.name template. Do not accept arbitrary colons.
        for (const std::string prefix : {"最速到達：", "最速到達: "})
            if (text.compare(0, prefix.size(), prefix) == 0) return prefix.size();
        return 0;
    }
};
