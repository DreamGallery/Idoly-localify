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
        return isNumericCount(value.size(), [&](size_t i) { return value[i]; });
    };
    for (const auto value : {"0", "284", "1930", "10,000", "1,234,567", "  10,000\n"}) {
        assert(numeric(value));
        assert(!usesReplacement("Currency/Count", numeric(value)));
    }
    for (const auto value : {"", " ", ",", "1234,567", "1,23", "1,,000", "1,000,", "1 000",
        "PR", "6GB", "Lv284", "12:00", "1/10", "10%", "<size=20>1000</size>"}) {
        assert(!numeric(value));
    }
    struct Char { int unicode; };
    std::vector<Char> terminated{{'1'},{','},{'0'},{'0'},{'0'},{0},{'x'}};
    assert(isNumericBuffer(terminated.data(), terminated.size()));
    assert(!isNumericBuffer(terminated.data(), 5));
    assert(!isNumericBuffer<Char>(nullptr, 5));
    assert(!isNumericBuffer(terminated.data(), 65537));
    assert(!numeric(std::string(129, '1')));
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
}
