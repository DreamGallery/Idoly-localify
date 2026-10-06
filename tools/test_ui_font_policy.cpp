#include <cassert>
#include <string>
#include <vector>
#include "../app/src/main/cpp/ui_font_policy.h"

int main() {
    using namespace ui_font_policy;
    for (const auto path : originalFontPaths) {
        assert(!usesReplacement(path));
        assert(isOriginalFontCandidate(path.substr(path.rfind('/') + 1)));
        // The same template in an unrelated page is not a global exclusion.
        assert(usesReplacement("OtherWindow/" + std::string(path)));
        assert(usesReplacement(std::string(path) + "/Text"));
        auto wrongPage = std::string(path);
        const auto window = wrongPage.find("Window(Clone)");
        assert(window != std::string::npos);
        wrongPage.replace(window, 6, "Panel");
        assert(usesReplacement(wrongPage));
    }
    for (const auto path : shopPricePaths) {
        assert(needsPath(path.substr(path.rfind('/') + 1)));
        assert(!usesReplacement(path, false)); // Currency symbols / units stay native too.
        assert(!usesReplacement(path, true));
        assert(preRenderRoute(path, true, false, true, false) == PreRenderRoute::Original);
        assert(preRenderRoute(path, false, false, false, false) == PreRenderRoute::Original);
        assert(usesReplacement("Other/" + std::string(path), false));
        assert(usesReplacement(std::string(path) + "/Other", false));
        auto wrongCell = std::string(path);
        const auto at = wrongCell.find("PurchasePrice");
        if (at != std::string::npos) {
            wrongCell.replace(at, 13, "HeldBalance");
            assert(!isShopPrice(wrongCell));
        }
    }
    // Captured held-resource amount is not a purchase price; normal numeric
    // policy retains its font without extending the price exception to text.
    const std::string balance = "WindowSystem/WindowRoot/ShopWindow(Clone)/ExchangeDetailScreen(Clone)/ScreenCanvas/ScreenRoot/ScreenSplitArea/LowerSafe/ExchangeBar/ResourceLayoutGroup/AmountText";
    assert(!isShopPrice(balance));
    assert(!usesReplacement(balance, true));
    assert(usesReplacement(balance, false));
    assert(!isShopPrice("OtherWindow/Price"));
    // No text whitelist is needed: future labels, tags, names and counters
    // follow the default, while similarly named controls remain independent.
    for (const auto path : {"", "PR", "OtherScreen/BigTitle", "OtherScreen/Rank",
        "GalleryCaseListCell(Clone)/HashtagRoot/Hashtag1/Text",
        "FriendCardListCell(Clone)/ProfileDataGroup/Name/NameText",
        "FriendCardListCell(Clone)/ProfileDataGroup/Level/Title",
        "FriendCardListCell(Clone)/ProfileDataGroup/Level/LevelText",
        "FriendCardListCell(Clone)/ProfileDataGroup/UserId/UserIdText",
        "HomeScreen/Currency", "StoryPlayer/Subtitles/Name", "HomeTalk/Serif"}) {
        assert(usesReplacement(path));
    }
    assert(!isOriginalFontCandidate("Text"));
    assert(!isOriginalFontCandidate("NameText"));
    assert(!isOriginalFontCandidate("BigTitle2"));
    const auto numeric = [](std::string_view value) {
        return isNumericSymbolText(value.size(), [&](size_t i) { return value[i]; });
    };
    for (const auto value : {"0", "284", "1930", "10,000", "1,234,567", "  10,000\n",
        "+1655", "+1,655", "-12.5", "12:00", "08:00:00", "1/10", "10%", "(123)",
        "$1.99", "1~5", "1 000", "1,23", "1,,000"}) {
        assert(numeric(value));
        assert(!usesReplacement("Currency/Count", numeric(value)));
    }
    for (const auto value : {"", " ", ",", "+", "...", "PR", "6GB", "Lv284", "360LIVE",
        "<size=20>1000</size>", "<color=#ff0000>+1655</color>", "<sprite=1>", "1x2", "1e3"}) {
        assert(!numeric(value));
    }
    const auto numeric16 = [](std::u16string_view value) {
        return isNumericSymbolText(value.size(), [&](size_t i) { return value[i]; });
    };
    for (const auto value : {u"＋1655", u"＋１，６５５", u"－１２．５％", u"×3", u"÷2", u"−12",
        u"±20", u"￥1,200", u"€12.5", u"（1／10）", u"1～5", u"1〜5", u"1\u202f000", u"30°"})
        assert(numeric16(value));
    for (const auto value : {u"……", u"＋", u"1人", u"剩余3次", u"1回", u"1分", u"Ｌｖ１２",
        u"α1", u"体力＋1655", u"1\ue000", u"1\U0001f600"})
        assert(!numeric16(value));
    struct Char { int unicode; };
    std::vector<Char> terminated{{'1'},{','},{'0'},{'0'},{'0'},{0},{'x'}};
    assert(isNumericSymbolBuffer(terminated.data(), terminated.size()));
    assert(!isNumericSymbolBuffer(terminated.data(), 5));
    assert(!isNumericSymbolBuffer<Char>(nullptr, 5));
    assert(!isNumericSymbolBuffer(terminated.data(), 65537));
    assert(!numeric(std::string(129, '1')));
    // Both actual glyph buffers and string setters share the classification.
    // A formatted field's final buffer remains authoritative over a stale getter.
    const std::string stamina = "WindowRoot/RefreshLevelSelectCharacterCell(Clone)/RecoveryStaminaOverflow";
    std::vector<Char> signedAmount{{'+'},{'1'},{'6'},{'5'},{'5'},{0}};
    assert(isNumericSymbolBuffer(signedAmount.data(),signedAmount.size()));
    assert(!usesReplacement(stamina,isNumericSymbolBuffer(signedAmount.data(),signedAmount.size())));
    assert(preRenderRoute(stamina,true,true,true,false) == PreRenderRoute::Original);
    assert(preRenderRoute(stamina,true,false,true,true) == PreRenderRoute::Replacement);
    assert(usesReplacement(stamina,numeric16(u"体力＋1655")));
    // The requested uniform 360°LIVE title remains an explicit replacement
    // exception; treating 360° as an ordinary number would split its fonts.
    assert(numeric16(u"360°"));
    for (const auto root : {panorama_font_labels::labelRoot, panorama_font_labels::musicTitleRoot}) {
        for (const auto* leaf : {"360", "LIVE"}) {
            const auto path = std::string(root) + leaf;
            assert(needsPath(leaf));
            assert(usesReplacement(path,true));
            assert(preRenderRoute(path,true,true,true,true) == PreRenderRoute::Replacement);
            assert(!usesReplacement(path + "/Other",true));
        }
    }
    assert(!usesReplacement("Other/360",true));
    const std::string friendRoot = "ShowcaseGalleryTopScreen(Clone)/ShowcaseGalleryFriendSearchSheet(Clone)/Canvas/Root/UIContentArea/SheetContainer/ContentContainer/ContentMask/ContentViewport/Content/FriendCardListCell(Clone)/Button/ProfileCard/ProfileDataGroup/";
    for (const auto leaf : {"Name/NameText", "Level/Title", "Level/LevelText"}) {
        assert(usesReplacement(friendRoot + leaf, true));
        assert(usesReplacement("WindowRoot/ProfileTopScreen(Clone)/FriendPage/List/FriendCardListCell(Clone)/Button/ProfileCard/ProfileDataGroup/" + std::string(leaf), true));
    }
    assert(!usesReplacement(friendRoot + "UserId/UserIdText", true));
    assert(!usesReplacement("ProfileCard/ProfileDataGroup/Level/LevelText", true));
    assert(!usesReplacement(friendRoot + "Level/LevelText/Child", true));
    for (const auto path : originalFontPaths) {
        if (path.substr(path.rfind('/') + 1) != "Rank") continue;
        auto title = std::string(path);
        title.replace(title.rfind("Rank"), 4, "Title");
        assert(usesReplacement(title)); // VENUS RANK label, native Di145 below.
    }
    assert(preRenderRoute("Currency/Count", true, true, true, false) == PreRenderRoute::Original);
    assert(preRenderRoute("Title", true, false, true, true) == PreRenderRoute::Replacement);
    assert(preRenderRoute("Currency/Count", false, false, true, true) == PreRenderRoute::Defer);
    assert(preRenderRoute("FormattedLabel", false, false, false, false) == PreRenderRoute::Defer);
    assert(preRenderRoute("PR", false, false, true, false) == PreRenderRoute::Replacement);
    assert(preRenderRoute(originalFontPaths[0], true, false, true, false) == PreRenderRoute::Original);
    assert(preRenderRoute(friendRoot + "Level/LevelText", true, true, true, true) == PreRenderRoute::Replacement);
    const std::string guildTotal = "WindowSystem/WindowRoot/LiveWindow(Clone)/GuildWindow(Clone)/GuildSelectScreen(Clone)/ScreenCanvas/ScreenRoot/PreviousSeason/TotalPt";
    const std::string guildPoint = "WindowRoot/LiveWindow(Clone)/GuildWindow(Clone)/GuildSelectScreen(Clone)/GuildRankingSheet(Clone)/Canvas/Root/UIContentArea/SheetContainer/ContentContainer/ContentMask/ContentViewport/Content/GuildRankingListCell(Clone)/cell/Point";
    assert(needsPath("TotalPtNum") && needsPath("Point"));
    assert(!usesReplacement(guildTotal + "Num", false)); // The numeric field includes the Pt suffix.
    assert(!usesReplacement(guildPoint, false));
    assert(preRenderRoute(guildTotal + "Num", true, false, true, false) == PreRenderRoute::Original);
    assert(preRenderRoute(guildPoint, true, false, true, false) == PreRenderRoute::Original);
    assert(preRenderRoute(guildPoint, false, false, false, false) == PreRenderRoute::Original); // Formatted getter may be empty/stale.
    assert(usesReplacement(guildTotal)); // Chinese label remains replacement.
    assert(usesReplacement(guildPoint + "Label"));
    assert(usesReplacement("Other/TotalPtNum"));
    assert(usesReplacement("Other/Point"));
    const std::string skipRoot = "ActGameShortenItemActionSheet(Clone)/Canvas/Root/UIContentArea/SheetContainer/ContentContainer/ContentMask/ContentViewport/Content/EffectAmount/AmountRoot/";
    for (const auto* screen : {"PromotionScreen(Clone)", "RefreshScreen(Clone)"}) {
        for (const auto* leaf : {"BeforeAmount", "AfterAmount"}) {
            const auto path = "WindowSystem/WindowRoot/ActivityWindow(Clone)/" + std::string(screen) + "/" + skipRoot + leaf;
            assert(needsPath(leaf));
            assert(isActivitySkipTime(path));
            assert(keepsOriginalDesign(path));
            // The explicit timer path also preserves the original font before
            // a formatted glyph buffer has become available.
            assert(!usesReplacement(path, false));
            assert(preRenderRoute(path, true, false, true, false) == PreRenderRoute::Original);
            assert(preRenderRoute(path, false, false, false, false) == PreRenderRoute::Original);
            assert(usesReplacement(path + "/Child", false));
        }
    }
    assert(!isActivitySkipTime("Other" + skipRoot + "BeforeAmount"));
    assert(usesReplacement("Other/BeforeAmount"));
    assert(usesReplacement("Other/AfterAmount"));
    assert(usesReplacement(skipRoot + "Title"));
    const std::string receiveRoot = "WindowSystem/WindowRoot/ActivityWindow(Clone)/PromotionScreen(Clone)/ScreenCanvas/ScreenRoot/ReceieveResult/Dialog/";
    for (const auto* child : {"ActivityLevel/ActivityInfo/Level", "ManagerExp/Level", "PromotionReceiveResultCharacter/Status/GaugeRoot/Level"}) {
        assert(needsPath("Level"));
        assert(!usesReplacement(receiveRoot + child));
        assert(preRenderRoute(receiveRoot + child, true, false, true, false) == PreRenderRoute::Original);
        assert(usesReplacement(receiveRoot + child + "/Child"));
    }
    assert(activity_result_layout::activityName(receiveRoot + "ActivityLevel/ActivityInfo/Name"));
    assert(activity_result_layout::aileLabel(receiveRoot + "RewardsContainer/ActivityResultRewardView(Clone)/FadeResourceThumbnail/AIleNoti/Aile"));
    assert(!activity_result_layout::activityName("OtherPromotionScreen(Clone)/ScreenCanvas/ScreenRoot/ReceieveResult/Dialog/ActivityLevel/ActivityInfo/Name"));
    for (const auto* screen : {"PromotionResultScreen(Clone)", "FanEventResultScreen(Clone)", "RefreshResultScreen(Clone)"}) {
        const auto root = "WindowSystem/WindowRoot/ActivityWindow(Clone)/" + std::string(screen) +
            "/ScreenCanvas/ScreenRoot/Container/Information/";
        for (const auto* child : {"ActivityResultActivityLevel/TextLayoutGroup/ActivityLevel",
            "ActivityResultActivityLevel/Gauge/GaugeValue", "ActivityResultManagerExp/ManagerExpGauge/ManagerLevel",
            "ActivityResultManagerExp/ManagerExpGauge/Gauge/GaugeValue", "ActivityResultManagerExp/ManagerPointUp"}) {
            const auto path = root + child;
            assert(needsPath(path.substr(path.rfind('/') + 1)));
            assert(!usesReplacement(path, false)); // Lv48/48, 35942629/188717174 and +283.
            assert(keepsOriginalDesign(path));
            assert(preRenderRoute(path, true, false, false, false) == PreRenderRoute::Original);
            assert(preRenderRoute(path, false, false, true, false) == PreRenderRoute::Original);
            assert(usesReplacement(path + "/Child"));
        }
        const auto label = root + "ActivityResultActivityLevel/TextLayoutGroup/ActivityName";
        assert(usesReplacement(label));
        assert(activity_result_layout::activityName(label));
        assert(!activity_result_layout::activityName(label + "/Child"));
        const auto aile = root + "ActivityResultRewards/Rewards/ActivityResultRewardView(Clone)/FadeResourceThumbnail/AIleNoti/Aile";
        assert(activity_result_layout::aileLabel(aile));
        assert(usesReplacement(aile));
        assert(!activity_result_layout::aileLabel(aile + "/Child"));
    }
    for (const auto* path : {"Other/ManagerPointUp", "Other/GaugeValue", "Other/ActivityLevel",
        "Other/ManagerLevel", "Other/ActivityName", "Other/Aile",
        "OtherPromotionResultScreen(Clone)/ScreenCanvas/ScreenRoot/Container/Information/ActivityResultManagerExp/ManagerPointUp",
        "PromotionScreen(Clone)/ScreenCanvas/ScreenRoot/Container/Information/ActivityResultManagerExp/ManagerPointUp"}) {
        assert(usesReplacement(path));
        assert(!activity_result_layout::activityName(path));
        assert(!activity_result_layout::aileLabel(path));
    }
    // These English captions scramble letters during the game's animation.
    // Scope by prefab, not by the temporary string or its final phrase.
    for (const auto* screen : {"PromotionScreen(Clone)", "FanEventScreen(Clone)", "RefreshScreen(Clone)"}) {
        const auto root = "WindowSystem/WindowRoot/ActivityWindow(Clone)/" + std::string(screen) +
            "/ScreenCanvas/ScreenRoot/ActGameFinish/";
        for (const auto* child : {"FinishTextPositionRoot/FinishTextAnimationRoot/FinishText",
            "ThanksTextPositionRoot/ThanksTextAnimationRoot/ThanksText"}) {
            const auto path = root + child;
            assert(needsPath(path.substr(path.rfind('/') + 1)));
            assert(!usesReplacement(path, false));
            assert(keepsOriginalDesign(path));
            assert(preRenderRoute(path, false, false, false, false) == PreRenderRoute::Original);
            assert(preRenderRoute(path, true, false, true, false) == PreRenderRoute::Original);
            assert(!activity_result_layout::finishAnimatedText(path + "/Child"));
        }
        assert(!activity_result_layout::finishAnimatedText(root + "Other/FinishText"));
    }
    assert(usesReplacement("Other/FinishText"));
    assert(usesReplacement("Other/ThanksText"));
    assert(!activity_result_layout::finishAnimatedText("OtherRefreshScreen(Clone)/ScreenCanvas/ScreenRoot/ActGameFinish/FinishTextPositionRoot/FinishTextAnimationRoot/FinishText"));
}
