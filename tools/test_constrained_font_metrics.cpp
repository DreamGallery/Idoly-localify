#include "../app/src/main/cpp/constrained_font_metrics.h"
#include <algorithm>
#include <cassert>
#include <limits>
int main() {
    const std::string path = "WindowSystem/WindowRoot/ForumWindow(Clone)/ForumListScreen(Clone)/ScreenCanvas/ScreenRoot/ContentRoot/ForumThreadRoot/ForumThreadList/Scroll/Viewport/Content/ForumThreadListCell(Clone)/Button/Title";
    const FontVerticalMetrics replacement{90,1,104.4f,-25.92f}, original{34,1,29.92f,-4.08f};
    assert(constrainedTitleMetricsGuard(path,740,49,48,1,replacement,original));
    assert(constrainedTitleMetricsGuard(path,740,49,48,3,replacement,original));
    assert(!constrainedTitleMetricsGuard(path,740,49,48,0,replacement,original));
    assert(!constrainedTitleMetricsGuard(path,740,49,40,1,replacement,original));
    assert(!constrainedTitleMetricsGuard(path,740,80,48,1,replacement,original));
    assert(!constrainedTitleMetricsGuard("Other/Title",740,49,48,1,replacement,original));
    assert(!constrainedTitleMetricsGuard(path,740,49,48,1,replacement,replacement));
    assert(!constrainedTitleMetricsGuard(path,740,49,48,1,original,original));
    const std::string history = "Canvas/Root/UIContentArea/SheetContainer/ContentContainer/ContentMask/ContentViewport/Content/TalkHistoryPageList/SolisScrollView/Viewport/Content/TalkHistoryPageListCell(Clone)/HistoryView/Choise";
    for (int i = 1; i <= 4; ++i) {
        const auto title = history + std::to_string(i) + "/Title";
        assert(constrainedTitleMetricsGuard(title,500,50,48,1,replacement,original));
        assert(constrainedTitleMetricsGuard(title,500,50,48,3,replacement,original));
        assert(!constrainedTitleMetricsGuard(title,500,50,48,0,replacement,original));
        assert(!constrainedTitleMetricsGuard(title,500,70,48,1,replacement,original));
        assert(!constrainedTitleMetricsGuard(title,500,50,32,1,replacement,original));
        assert(!constrainedTitleMetricsGuard(title,500,50,48,1,original,original));
        assert(constrainedTitle(history + std::to_string(i) + "/DateTime") == ConstrainedTitle::None);
    }
    assert(constrainedTitle(history + "5/Title") == ConstrainedTitle::None);
    assert(constrainedTitle(history + "1/Title/Child") == ConstrainedTitle::None);
    assert(constrainedTitle("Other/HistoryView/Choise1/Title") == ConstrainedTitle::None);
    const std::string choice = "WindowRoot/LoveWindow(Clone)/LoveAdvPlayerScreen(Clone)/LoveADVEnginePresenter/LoveADVEngine/UI/Canvas/Root/UI Layer/Love UI/Choice Canvas/Choice View/Choice Group/Choice Button ";
    for (int i = 1; i <= 3; ++i) {
        const auto text = choice + std::to_string(i) + "/Animation Root/Text";
        assert(constrainedTitle(text) == ConstrainedTitle::LoveChoice);
        assert(constrainedTitleMetricsGuard(text,688,48,48,0,replacement,original));
        assert(!constrainedTitleMetricsGuard(text,688,48,33.1f,0,replacement,original)); // Validate at max size, not the shrunken result.
        assert(!constrainedTitleMetricsGuard(text,688,48,48,1,replacement,original));
        assert(!constrainedTitleMetricsGuard(text,688,96,48,0,replacement,original));
        assert(!constrainedTitleMetricsGuard(text,688,48,48,0,original,original));
        assert(constrainedTitle(text + "/Child") == ConstrainedTitle::None);
    }
    assert(constrainedTitle(choice + "4/Animation Root/Text") == ConstrainedTitle::None);
    assert(constrainedTitle("Other/Choice Button 1/Animation Root/Text") == ConstrainedTitle::None);
    const std::string medal = "WindowSystem/WindowRoot/LiveWindow(Clone)/GuildWindow(Clone)/GuildTopScreen(Clone)/ScreenCanvas/ScreenRoot/UnionMedal/Amount";
    assert(constrainedTitle(medal) == ConstrainedTitle::GuildMedal);
    assert(constrainedTitle("WindowSystem/WindowRoot/HomeWindow(Clone)/GuildWindow(Clone)/GuildTopScreen(Clone)/ScreenCanvas/ScreenRoot/UnionMedal/Amount") == ConstrainedTitle::GuildMedal);
    assert(constrainedTitle(medal + "/Other") == ConstrainedTitle::None);
    assert(constrainedTitleUsesAutoSize(ConstrainedTitle::GuildMedal));
    assert(constrainedTitleMetricsGuard(medal,360,33,32,0,replacement,original));
    // Captured size 22.75 is the result of height fitting. Validate against
    // maximum 32; the replacement's 46.34 line height cannot fit the 33 box.
    assert(!constrainedTitleMetricsGuard(medal,360,33,22.75f,0,replacement,original));
    assert(!constrainedTitleMetricsGuard(medal,360,50,32,0,replacement,original));
    assert(!constrainedTitleMetricsGuard(medal,500,33,32,0,replacement,original));
    assert(!constrainedTitleMetricsGuard(medal,360,33,32,1,replacement,original));
    assert(!constrainedTitleMetricsGuard(medal,360,33,32,0,original,original));
    assert(constrainedTitle(medal + "/Child") == ConstrainedTitle::None);
    assert(constrainedTitle("Other/UnionMedal/Amount") == ConstrainedTitle::None);
    assert(!constrainedTitleUsesAutoSize(ConstrainedTitle::TalkHistory));
    assert(fontLineHeight(32,replacement) > 33 && fontLineHeight(32,original) <= 33);
    assert(fontLineHeight(48,original) <= 49);
    for (const auto* screen : {"PromotionResultScreen(Clone)", "FanEventResultScreen(Clone)", "RefreshResultScreen(Clone)"}) {
        const auto name = "WindowSystem/WindowRoot/ActivityWindow(Clone)/" + std::string(screen) +
            "/ScreenCanvas/ScreenRoot/Container/Information/ActivityResultActivityLevel/TextLayoutGroup/ActivityName";
        assert(constrainedTitle(name) == ConstrainedTitle::ActivityName);
        assert(!constrainedTitleUsesAutoSize(ConstrainedTitle::ActivityName));
        assert(constrainedTitleMetricsGuard(name, 0, 0, 48, 0, replacement, original)); // Before FitToChild.
        assert(constrainedTitleMetricsGuard(name, 96.01f, 69.51f, 48, 0, replacement, original));
        assert(constrainedTitleMetricsGuard(name, 144.01f, 48.01f, 48, 0, replacement, original)); // After normalization.
        assert(!constrainedTitleMetricsGuard(name, 96, 96, 48, 0, replacement, original));
        assert(!constrainedTitleMetricsGuard(name, 96, 48, 40, 0, replacement, original));
        assert(!constrainedTitleMetricsGuard(name, 96, 48, 48, 1, replacement, original));
        assert(!constrainedTitleMetricsGuard(name, 96, 48, 48, 0, original, original));
        assert(constrainedTitle(name + "/Child") == ConstrainedTitle::None);
    }
    assert(constrainedTitle("Other/ActivityName") == ConstrainedTitle::None);
    const std::string received = "WindowSystem/WindowRoot/ActivityWindow(Clone)/PromotionScreen(Clone)/ScreenCanvas/ScreenRoot/ReceieveResult/Dialog/ActivityLevel/ActivityInfo/Name";
    assert(constrainedTitleCandidateName("Name"));
    assert(constrainedTitle(received) == ConstrainedTitle::ActivityName);
    assert(constrainedTitleMetricsGuard(received,96.01f,69.51f,48,0,replacement,original));
    assert(constrainedTitleMetricsGuard(received,96.01f,48.01f,48,0,replacement,original));
    assert(!constrainedTitleMetricsGuard(received,96.01f,48.01f,40,0,replacement,original));
    assert(constrainedTitle(received + "/Child") == ConstrainedTitle::None);
    // Runtime capture: 360 LIVE's 32-high song title lost all geometry with a
    // 46.336-high replacement line. Keep the original 32 size and ellipsis.
    const std::string music = "WindowSystem/WindowRoot/PanoramaWindow(Clone)/PanoramaMusicSelectScreen(Clone)/ScreenCanvas/ScreenRoot/PanoramaMusicList/Scroll/Viewport/Content/PanoramaMusicListCell(Clone)/MusicThumbnail/SizeAdjustScaleRoot/ButtonScaleRoot/FrontRoot/MusicName";
    assert(constrainedTitle(music) == ConstrainedTitle::ClippedLabel);
    assert(constrainedTitleMetricsGuard(music,268,32,32,1,replacement,original));
    assert(!constrainedTitleMetricsGuard(music,268,64,32,1,replacement,original));
    assert(!constrainedTitleMetricsGuard(music,268,32,32,0,replacement,original));
    assert(!constrainedTitleMetricsGuard(music,268,32,32,1,replacement,replacement));
    assert(!constrainedTitleUsesAutoSize(constrainedTitle(music)));
    // A 30..32 autosize range still cannot fit a 32-high item label. Normalize
    // at the original maximum, allowing the game to fit long names by width.
    const std::string reward = "Canvas/Root/ReceiveAllResultGridListCell(Clone)/base/Name";
    assert(constrainedTitle(reward) == ConstrainedTitle::ClippedAutoLabel);
    assert(constrainedTitleUsesAutoSize(constrainedTitle(reward)));
    assert(fontLineHeight(30,replacement) > 32);
    assert(constrainedTitleMetricsGuard(reward,301,32,32,1,replacement,original));
    assert(!constrainedTitleMetricsGuard(reward,301,32,30,1,replacement,original));
    // Verify every guarded prefab can reach the glyph-lookup routing filter,
    // cannot match a similarly named unrelated label, and fails closed after
    // a game layout/font update. The suffix must fit the 16-ancestor capture.
    for (const auto& rule : clippedLabelMetricsRules) {
        const std::string label = "WindowRoot" + std::string(rule.suffix);
        const auto leaf = rule.suffix.substr(rule.suffix.rfind('/') + 1);
        assert(constrainedTitleCandidateName(leaf));
        assert(std::count(rule.suffix.begin(),rule.suffix.end(),'/') < 16);
        assert(constrainedTitleUsesAutoSize(constrainedTitle(label)) == rule.autoSize);
        const float width = rule.width < 0 ? 600 : rule.width;
        assert(constrainedTitleMetricsGuard(label,width,rule.height,rule.size,1,replacement,original));
        assert(constrainedTitleMetricsGuard(label,width,rule.height,rule.size,3,replacement,original));
        assert(!constrainedTitleMetricsGuard(label,width,rule.height+20,rule.size,1,replacement,original));
        assert(!constrainedTitleMetricsGuard(label,width,rule.height,rule.size+2,1,replacement,original));
        assert(!constrainedTitleMetricsGuard(label,width,rule.height,rule.size,1,original,original));
        assert(!constrainedTitleMetricsGuard(label,-1,rule.height,rule.size,1,replacement,original));
        assert(!constrainedTitleMetricsGuard(label,width,std::numeric_limits<float>::quiet_NaN(),rule.size,1,replacement,original));
        if (rule.width >= 0)
            assert(!constrainedTitleMetricsGuard(label,width+20,rule.height,rule.size,1,replacement,original));
        else
            assert(constrainedTitleMetricsGuard(label,1000,rule.height,rule.size,1,replacement,original));
        assert(constrainedTitle(label + "/Child") == ConstrainedTitle::None);
        assert(constrainedTitle("Other/" + std::string(leaf)) == ConstrainedTitle::None);
        assert(constrainedTitle("WindowRoot/Not" + std::string(rule.suffix.substr(1))) == ConstrainedTitle::None);
    }
}
