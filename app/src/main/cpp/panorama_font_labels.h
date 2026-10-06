#pragma once
#include <cmath>
#include <cstddef>
#include <string_view>

namespace panorama_font_labels {
inline constexpr std::string_view labelRoot =
    "WindowSystem/WindowRoot/HomeWindow(Clone)/HomeTopScreen(Clone)/ScreenCanvas/ScreenRoot/CharacterPage/SlideRoot/Character/HomeRenewalRoot/Panorama/LabelRoot/";
inline constexpr std::string_view musicTitleRoot =
    "WindowSystem/WindowRoot/PanoramaWindow(Clone)/PanoramaMusicSelectScreen(Clone)/ScreenCanvas/ScreenRoot/HeaderRoot/TitleRoot/";

inline std::string_view labelLeaf(std::string_view path) {
    for (auto root : {labelRoot, musicTitleRoot}) {
        if (path.substr(0, root.size()) != root) continue;
        const auto leaf = path.substr(root.size());
        if (leaf == "360" || leaf == "LIVE") return leaf;
    }
    return {};
}

inline float musicTitleGap(float parentWidth) {
    return std::isfinite(parentWidth) && parentWidth > 0.0f ? parentWidth * 0.02f : 0.0f;
}

// Accept TMP UnicodeChar buffers without coupling this helper to the router.
// capacity is the actual IL2CPP array length; bytes after the first NUL are stale.
template <typename UnicodeChar>
inline bool shouldUseReplacement(std::string_view path, const UnicodeChar* text, size_t capacity) {
    if (!text || capacity < 5 || capacity > 65536) return false;
    const auto leaf = labelLeaf(path);
    const std::u32string_view expected = leaf == "360" ? U"360°" : leaf == "LIVE" ? U"LIVE" : U"";
    if (expected.empty()) return false;
    for (size_t i = 0; i < expected.size(); ++i)
        if (text[i].unicode != static_cast<int>(expected[i])) return false;
    return text[expected.size()].unicode == 0;
}

struct Layout {
    bool valid = false;
    double scale = 1.0; // Multiply BOTH labels' same original font size by this.
    double gap = 0.0;
    double leftWidth = 0.0;
    double rightWidth = 0.0;
    double leftCenterX = 0.0;
    double rightCenterX = 0.0;
};

// All inputs are local RectTransform units. Measure preferred widths using the
// same unscaled baseline font size each time; measuring the prior result causes
// oscillation. Output X coordinates are relative to the PARENT RECT CENTER,
// independent of its pivot. Set both children to centered anchors/pivots (0.5)
// and their rect widths to these output widths. Preserve Y and height separately.
inline Layout centeredLayout(double parentWidth, double leftPreferredWidth, double rightPreferredWidth) {
    Layout out;
    if (!std::isfinite(parentWidth) || !std::isfinite(leftPreferredWidth) ||
        !std::isfinite(rightPreferredWidth) || parentWidth <= 0.0 ||
        leftPreferredWidth <= 0.0 || rightPreferredWidth <= 0.0) return out;
    const double total = leftPreferredWidth + rightPreferredWidth;
    if (!std::isfinite(total)) return out;
    out.gap = parentWidth * 0.02;
    const double available = parentWidth - out.gap;
    out.scale = total > available ? available / total : 1.0;
    if (!(out.scale > 0.0)) return Layout{};
    out.leftWidth = leftPreferredWidth * out.scale;
    out.rightWidth = rightPreferredWidth * out.scale;
    out.leftCenterX = -(out.rightWidth + out.gap) * 0.5;
    out.rightCenterX = (out.leftWidth + out.gap) * 0.5;
    out.valid = true;
    return out;
}
} // namespace panorama_font_labels
