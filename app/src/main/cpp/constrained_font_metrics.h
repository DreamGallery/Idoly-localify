#pragma once
#include <cmath>
#include <string>
#include <string_view>
#include "activity_result_layout.h"
#include "guild_medal_font.h"
struct FontVerticalMetrics { float pointSize, scale, ascent, descent; };
inline float fontLineHeight(float size, FontVerticalMetrics m) {
    return m.pointSize > 0 ? (m.ascent - m.descent) * size * m.scale / m.pointSize : 0;
}
enum class ConstrainedTitle { None, Forum, TalkHistory, LoveChoice, GuildMedal, ActivityName, ClippedLabel, ClippedAutoLabel };
struct ClippedLabelMetricsRule {
    std::string_view suffix;
    float width, height, size;
    bool autoSize;
};
// These single-line prefabs use Ellipsis/Truncate. Negative width denotes a
// parent/layout-controlled width; height and size still have exact guards.
// Keep the replacement glyphs and horizontal fitting, using only the original
// vertical metrics on an independent font asset for this label.
inline constexpr ClippedLabelMetricsRule clippedLabelMetricsRules[] = {
    {"/PanoramaMusicListCell(Clone)/MusicThumbnail/SizeAdjustScaleRoot/ButtonScaleRoot/FrontRoot/MusicName", 268, 32, 32, false},
    {"/CharacterMusicListCell(Clone)/MusicName", 357, 51, 40, false},
    {"/LivePhotographyMusicListCell(Clone)/Music/MusicName", 336, 44, 32, false},
    {"/LivePhotographyStageListCell(Clone)/Stage/StageName", 388, 44, 32, false},
    {"/MainLiveAreaSelectScreen(Clone)/ScreenCanvas/ScreenRoot/Bottom/MainLiveAreaInfo/Black/AreaName", 696.25f, 50, 48, false},
    {"/EquippedPhotoListCell(Clone)/PhotoSelectCellView/Name", -1, 32, 32, false},
    {"/CharacterPhotoListCell(Clone)/NameLabel", -1, 32, 32, false},
    {"/GachaHistoryListCell(Clone)/Layout/Layout/GachaName", -1, 47, 40, false},
    {"/ShelfHomeScreen(Clone)/ScreenCanvas/ScreenRoot/ShelfHomeSlideTab/TabPageContainer/ShelfHomeAlbumTab/UpperContent/SelectSlotRoot/SelectSlotListRoot/Scroll/Viewport/LayoutGroup/SlotRoot/Name", -1, 32, 32, false},
    {"/ReceiveAllResultGridListCell(Clone)/base/Name", 301, 32, 32, true},
    {"/LoveReceiveAllResultGridListCell(Clone)/base/Name", 301, 32, 32, true},
    {"/RaceLessonResetSheet(Clone)/Canvas/Root/UIContentArea/SheetContainer/ContentContainer/ContentMask/ContentViewport/Content/racelessonEnhance/Name", 301, 32, 32, true},
    {"/StoryPlayerScreen(Clone)/StoryFullScreenLoading/Root/Layout/SmallTitle", -1, 100, 100, true},
    // The fixed "选择消息" prompt has a shared compatibility font already.
    // Also cover a different message assigned to the same button later.
    {"/MessageHistoryScreen(Clone)/ScreenCanvas/ScreenRoot/MessageDetailFooter/MessageSelectButton/Text", -1, 48, 48, false},
    {"/MessageDetailScreen(Clone)/ScreenCanvas/ScreenRoot/MessageDetailFooter/MessageSelectButton/Text", -1, 48, 48, false},
};
inline const ClippedLabelMetricsRule* clippedLabelMetricsRule(std::string_view path) {
    for (const auto& rule : clippedLabelMetricsRules)
        if (path.size() >= rule.suffix.size() && path.substr(path.size() - rule.suffix.size()) == rule.suffix)
            return &rule;
    return nullptr;
}
inline bool constrainedTitleCandidateName(std::string_view name) {
    return name == "Title" || name == "Text" || name == "Amount" || name == "ActivityName" || name == "Name" ||
        name == "MusicName" || name == "StageName" || name == "AreaName" || name == "Name" ||
        name == "NameLabel" || name == "GachaName" || name == "SmallTitle";
}
inline ConstrainedTitle constrainedTitle(const std::string& path) {
    if (const auto* rule = clippedLabelMetricsRule(path))
        return rule->autoSize ? ConstrainedTitle::ClippedAutoLabel : ConstrainedTitle::ClippedLabel;
    if (activity_result_layout::activityName(path)) return ConstrainedTitle::ActivityName;
    if (guild_medal_font::matches(path))
        return ConstrainedTitle::GuildMedal;
    static const std::string suffix = "/ForumWindow(Clone)/ForumListScreen(Clone)/ScreenCanvas/ScreenRoot/ContentRoot/ForumThreadRoot/ForumThreadList/Scroll/Viewport/Content/ForumThreadListCell(Clone)/Button/Title";
    if (path.size() >= suffix.size() && path.compare(path.size() - suffix.size(), suffix.size(), suffix) == 0)
        return ConstrainedTitle::Forum;
    // Both call history and past exchanges reuse these four single-line cells.
    // Keep the prefab's spelling (Choise) and match only its title children.
    static const std::string history = "/TalkHistoryPageList/SolisScrollView/Viewport/Content/TalkHistoryPageListCell(Clone)/HistoryView/Choise";
    const auto pos = path.rfind(history);
    if (pos != std::string::npos) {
        const auto tail = path.substr(pos + history.size());
        if (tail.size() == 7 && tail[0] >= '1' && tail[0] <= '4' && tail.substr(1) == "/Title")
            return ConstrainedTitle::TalkHistory;
    }
    static const std::string choice = "/Love UI/Choice Canvas/Choice View/Choice Group/Choice Button ";
    const auto choicePos = path.rfind(choice);
    if (choicePos != std::string::npos) {
        const auto tail = path.substr(choicePos + choice.size());
        if (tail.size() == 21 && tail[0] >= '1' && tail[0] <= '3' && tail.substr(1) == "/Animation Root/Text")
            return ConstrainedTitle::LoveChoice;
    }
    return ConstrainedTitle::None;
}
inline bool constrainedTitleUsesAutoSize(ConstrainedTitle kind) {
    return kind == ConstrainedTitle::LoveChoice || kind == ConstrainedTitle::GuildMedal || kind == ConstrainedTitle::ClippedAutoLabel;
}
inline bool constrainedTitleMetricsGuard(const std::string& path, float width, float height,
                                        float size, int overflow, FontVerticalMetrics replacement,
                                        FontVerticalMetrics original) {
    const auto kind = constrainedTitle(path);
    if (const auto* rule = clippedLabelMetricsRule(path)) {
        if (!std::isfinite(width) || !std::isfinite(height) || !std::isfinite(size) ||
            width < 0 || width > 4096 || (rule->width >= 0 && std::fabs(width - rule->width) > .5f) ||
            std::fabs(height - rule->height) > .5f || std::fabs(size - rule->size) > .5f ||
            (overflow != 1 && overflow != 3)) return false;
        const float replacementHeight = fontLineHeight(size, replacement), originalHeight = fontLineHeight(size, original);
        return std::isfinite(replacementHeight) && std::isfinite(originalHeight) &&
            replacementHeight > height + .5f && originalHeight > 0 && originalHeight <= height + .5f;
    }
    if (kind == ConstrainedTitle::ActivityName) {
        // FitToChild makes this box follow preferred height. Accept both its
        // original and replacement heights, including the initial empty rect;
        // never require the already inflated box to be too short for the face.
        const float replacementHeight = fontLineHeight(size, replacement);
        const float originalHeight = fontLineHeight(size, original);
        return std::isfinite(width) && std::isfinite(height) && std::isfinite(size) &&
            std::fabs(size - 48) <= .5f && overflow == 0 && width >= 0 && width <= 1024 &&
            height >= 0 && height <= replacementHeight + 1 &&
            std::isfinite(replacementHeight) && std::isfinite(originalHeight) &&
            std::fabs(originalHeight - 48) <= .5f && replacementHeight > originalHeight + .5f;
    }
    const bool choice = kind == ConstrainedTitle::LoveChoice;
    const bool medal = kind == ConstrainedTitle::GuildMedal;
    const float expectedWidth = medal ? 360 : choice ? 688 : (kind == ConstrainedTitle::Forum ? 740 : 500);
    const float expectedHeight = medal ? 33 : choice ? 48 : (kind == ConstrainedTitle::Forum ? 49 : 50);
    const float expectedSize = medal ? 32 : 48;
    if (kind == ConstrainedTitle::None || !std::isfinite(width) || !std::isfinite(height) || !std::isfinite(size) ||
        std::fabs(width - expectedWidth) > .5f || std::fabs(height - expectedHeight) > .5f || std::fabs(size - expectedSize) > .5f ||
        (constrainedTitleUsesAutoSize(kind) ? overflow != 0 : (overflow != 1 && overflow != 3))) return false;
    const float replacementHeight = fontLineHeight(size, replacement), originalHeight = fontLineHeight(size, original);
    return std::isfinite(replacementHeight) && std::isfinite(originalHeight) &&
        replacementHeight > height + .5f && originalHeight > 0 && originalHeight <= height + .5f;
}
