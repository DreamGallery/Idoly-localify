#pragma once
#include <cmath>
#include <string>
struct FontVerticalMetrics { float pointSize, scale, ascent, descent; };
inline float fontLineHeight(float size, FontVerticalMetrics m) {
    return m.pointSize > 0 ? (m.ascent - m.descent) * size * m.scale / m.pointSize : 0;
}
inline bool forumTitleMetricsGuard(const std::string& path, float width, float height,
                                  float size, int overflow, FontVerticalMetrics rhr,
                                  FontVerticalMetrics original) {
    static const std::string suffix = "/ForumWindow(Clone)/ForumListScreen(Clone)/ScreenCanvas/ScreenRoot/ContentRoot/ForumThreadRoot/ForumThreadList/Scroll/Viewport/Content/ForumThreadListCell(Clone)/Button/Title";
    if (path.size() < suffix.size() || path.compare(path.size() - suffix.size(), suffix.size(), suffix) ||
        std::fabs(width - 740) > .5f || std::fabs(height - 49) > .5f || std::fabs(size - 48) > .5f ||
        (overflow != 1 && overflow != 3)) return false;
    const float rhrHeight = fontLineHeight(size, rhr), originalHeight = fontLineHeight(size, original);
    return std::isfinite(rhrHeight) && std::isfinite(originalHeight) &&
        rhrHeight > height + .5f && originalHeight > 0 && originalHeight <= height + .5f;
}
