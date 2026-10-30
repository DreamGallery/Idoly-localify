#pragma once

#include <algorithm>
#include <cmath>
#include <optional>
#include <string_view>

namespace button_minimum_layout {

// Only a single, plain label in a centered, self-sizing TextButton frame.
// Rich text, multi-line descriptions and composite controls keep their layout.
inline bool plainLabel(std::string_view text) {
    return !text.empty() && text.size() <= 128 &&
        text.find_first_of("\r\n\t<>{}") == std::string_view::npos;
}

inline bool eligible(bool boundLabel, bool controlSelfSize, bool directChild,
                     int childAlignment) {
    return boundLabel && controlSelfSize && directChild &&
        childAlignment >= 0 && childAlignment <= 8 && childAlignment % 3 == 1;
}

inline std::optional<float> minimumWidth(float measuredWidth, float measuredHeight,
                                         float fontSize, float existingMinimum) {
    if (!std::isfinite(measuredWidth) || !std::isfinite(measuredHeight) ||
        !std::isfinite(fontSize) || !std::isfinite(existingMinimum) ||
        measuredWidth <= 0 || measuredWidth > 1024 || fontSize < 12 || fontSize > 100 ||
        measuredHeight <= 0 || measuredHeight > fontSize * 1.8f ||
        existingMinimum < -1 || existingMinimum > 1024) return std::nullopt;
    return std::max(measuredWidth, existingMinimum);
}

} // namespace button_minimum_layout
