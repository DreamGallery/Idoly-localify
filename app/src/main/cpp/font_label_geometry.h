#pragma once
#include <cmath>
#include <string>

// RectTransform anchoredPosition is relative to the interpolated anchor, not
// the parent's center. This keeps the child centered with any horizontal pivot.
inline bool centeredLabelX(float parentX, float parentWidth, float childWidth,
                           float anchorMin, float anchorMax, float pivot, float& x) {
    if (!std::isfinite(parentX) || !std::isfinite(parentWidth) ||
        !std::isfinite(childWidth) || !std::isfinite(anchorMin) ||
        !std::isfinite(anchorMax) || !std::isfinite(pivot) ||
        parentWidth < 1 || childWidth < 1 || childWidth > parentWidth ||
        anchorMin < 0 || anchorMax > 1 || anchorMin > anchorMax || pivot < 0 || pivot > 1)
        return false;
    const float anchor = parentX + parentWidth * (anchorMin + (anchorMax - anchorMin) * pivot);
    x = parentX + parentWidth * 0.5f - anchor + (pivot - 0.5f) * childWidth;
    return std::isfinite(x);
}

// CardSupport labels retain their Chinese layout profile in either full replacement
// routing mode. Latin home Rank labels need the old sizing only in ReplaceAll.
inline bool homeRankOrSupportLayoutCandidate(const std::string& label, bool replaceAll) {
    return label == "Label" || (replaceAll && label == "Rank");
}

// These two screens reuse button names; require the captured full hierarchy.
inline const char* costumeButtonProfile(const std::string& path) {
    const std::string preview = "WindowSystem/WindowRoot/Preview3dWindow(Clone)/CostumeFittingRoomScreen(Clone)/ScreenCanvas/ScreenRoot/UIRoot/Fotter/";
    const std::string selection = "WindowSystem/WindowRoot/CharacterWindow(Clone)/CharacterCostumeScreen(Clone)/ScreenCanvas/ScreenRoot/UIRoot/Fotter/";
    if (path == preview + "HideButton/Label") return "costume_preview_hide_ui";
    if (path == preview + "ReactionButton/Label") return "costume_preview_reaction";
    if (path == selection + "HideButton/Label") return "costume_selection_hide_ui";
    if (path == selection + "ReactionButton/Label") return "costume_selection_reaction";
    if (path == selection + "DetermineButton/Label") return "costume_selection_determine";
    return nullptr;
}

inline bool loveMissionLabel(const std::string& path, bool bulkReceive) {
    const std::string screen = "LoveWindow(Clone)/LoveMissionScreen(Clone)/ScreenCanvas/ScreenRoot/MissionTab/";
    const auto start = path.find(screen);
    if (start == std::string::npos || (start && path[start - 1] != '/')) return false;
    const auto local = path.substr(start + screen.size());
    if (bulkReceive) return local == "FloatingActionButton/Base/Text";
    const std::string tail = "/LoveMissionList/Scroll/Viewport/Content/LoveMissionListCell(Clone)/AnimationRoot/MissionCell/ReceiveButton/Text";
    return local.rfind("TabPageContainer/", 0) == 0 && local.size() > tail.size() &&
        local.compare(local.size() - tail.size(), tail.size(), tail) == 0;
}

inline bool guildTopButtonTitle(const std::string& path) {
    const std::string root = "WindowSystem/WindowRoot/LiveWindow(Clone)/GuildWindow(Clone)/GuildTopScreen(Clone)/ScreenCanvas/ScreenRoot/";
    return path == root + "LiveButton/Title" || path == root + "TrainingButton/Title";
}
