#pragma once
#include <cmath>
#include <string_view>
#include "constrained_font_metrics.h"

namespace guild_points_layout {
inline constexpr std::string_view seasonPath = "WindowSystem/WindowRoot/LiveWindow(Clone)/GuildWindow(Clone)/GuildSelectScreen(Clone)/ScreenCanvas/ScreenRoot/PreviousSeason/TotalPtNum";
inline constexpr std::string_view rankingPath = "WindowRoot/LiveWindow(Clone)/GuildWindow(Clone)/GuildSelectScreen(Clone)/GuildRankingSheet(Clone)/Canvas/Root/UIContentArea/SheetContainer/ContentContainer/ContentMask/ContentViewport/Content/GuildRankingListCell(Clone)/cell/Point";
enum class Kind { None, Season, Ranking };
inline Kind classify(std::string_view path) {
    return path == seasonPath ? Kind::Season : path == rankingPath ? Kind::Ranking : Kind::None;
}

inline bool scaledDescent(FontVerticalMetrics face, float size, float& result) {
    if (!std::isfinite(face.pointSize) || !std::isfinite(face.scale) ||
        !std::isfinite(face.descent) || !std::isfinite(size) ||
        face.pointSize <= 0 || face.scale <= 0 || size <= 0) return false;
    result = face.descent * size * face.scale / face.pointSize;
    return std::isfinite(result);
}

// For TMP Bottom alignment: baseline = rect bottom + bottom margin - descent.
// Return an absolute local Y from the sibling's baseline, never an accumulated
// offset. This continues to work when either label's pivot or size changes.
inline float bottomBaselineY(float labelY, float labelBottom, float labelMargin,
                             float labelDescent, float numberBottom, float numberMargin,
                             float numberDescent) {
    return labelY + labelBottom + labelMargin - labelDescent -
        numberBottom - numberMargin + numberDescent;
}
} // namespace guild_points_layout
