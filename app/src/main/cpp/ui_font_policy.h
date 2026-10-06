#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>
#include "guild_points_layout.h"
#include "activity_result_layout.h"
#include "panorama_font_labels.h"

namespace ui_font_policy {
// Default UI font is replacement. Retain observed decorative titles, compact rank
// badges, scoped counters and purchase-price labels whose original metrics are
// part of the layout.
inline constexpr std::string_view originalFontPaths[] = {
    "WindowSystem/WindowRoot/ShowcaseWindow(Clone)/ShowcaseTopScreen(Clone)/ScreenCanvas/ScreenRoot/ScreenWithHeroImage/FullScreen/Upper/HeaderImageContainer/TItleRoot/BigTitle",
    "WindowSystem/WindowRoot/StoryWindow(Clone)/CardStoryScreen(Clone)/ScreenCanvas/ScreenRoot/ScreenWithHeroImage/FullScreen/Upper/HeaderImageContainer/TItleRoot/BigTitle",
    "WindowSystem/WindowRoot/LiveWindow(Clone)/LiveTopScreen(Clone)/ScreenCanvas/ScreenRoot/ScreenWithHeroImage/FullScreen/Upper/HeaderImageContainer/TItleRoot/BigTitle",
    "WindowSystem/WindowRoot/HomeWindow(Clone)/HomeTopScreen(Clone)/ScreenCanvas/ScreenRoot/HomeCompanyButton/Button/Rank",
    "WindowSystem/WindowRoot/HomeWindow(Clone)/HomeTopScreen(Clone)/ScreenCanvas/ScreenRoot/HomeVenusRankButton/Button/Rank",
    "WindowSystem/WindowRoot/LiveWindow(Clone)/LiveTopScreen(Clone)/ScreenCanvas/ScreenRoot/ScreenWithHeroImage/FullScreen/Upper/HeaderFrontContainer/HomeVenusRankButton/Button/Rank",
    "WindowSystem/WindowRoot/LiveWindow(Clone)/LiveTopScreen(Clone)/ScreenCanvas/ScreenRoot/ScreenWithHeroImage/ScrollView/ScrollViewViewport/ScrollMask/BottomArea/TowerButton/Base/StageBase/Stage",
    guild_points_layout::seasonPath,
    guild_points_layout::rankingPath,
};

// Observed purchase prices (16-ancestor capture paths); never held balances.
inline constexpr std::string_view shopPricePaths[] = {
    "NormalShopScreen(Clone)/ScreenCanvas/ScreenRoot/ScreenSplitArea/LowerSafe/LayoutGroup/LowerArea/ShopItemGridList/Scroll/Viewport/Content/ShopItemGridListCell(Clone)/ShopItemThumbnail/FrontRoot/PurchasePrice/ProductPrice",
    "ScreenCanvas/ScreenRoot/ScreenSplitArea/LowerSafe/LayoutGroup/LowerArea/ShopItemGridList/Scroll/Viewport/Content/ShopItemGridListCell(Clone)/ShopItemThumbnail/FrontRoot/PurchasePrice/ConsumptionIcon/Price",
    "ScreenCanvas/ScreenRoot/ScreenSplitArea/LowerSafe/LayoutGroup/LowerArea/ShopItemGridList/Scroll/Viewport/Content/ShopItemGridListCell(Clone)/ShopItemThumbnail/FrontRoot/PurchasePrice/StoneAndGold/Price",
    "ShopWindow(Clone)/MarketScreen(Clone)/ScreenCanvas/ScreenRoot/ScreenSplitArea/LowerSafe/MarketItemGridList/Scroll/Viewport/Content/MarketItemGridListCell(Clone)/MarketItemThumbnail/FrontRoot/PurchasePrice/StoneAndGold/Price",
    "WindowRoot/ShopWindow(Clone)/StoneShopScreen(Clone)/ScreenCanvas/ScreenRoot/ScreenSplitArea/LowerSafe/ShopItemGridList/Scroll/Viewport/Content/ShopItemGridListCell(Clone)/ShopItemThumbnail/FrontRoot/PurchasePrice/ProductPrice",
    "WindowSystem/WindowRoot/ExchangeWindow(Clone)/ExchangeDetailScreen(Clone)/ScreenCanvas/ScreenRoot/ScreenSplitArea/LowerSafe/ExchangDetailList/ScrollView/Viewport/Content/ExchangeDetailListCell(Clone)/Footer/RequiredResourceAmount",
    "WindowSystem/WindowRoot/ShopWindow(Clone)/ExchangeDetailScreen(Clone)/ScreenCanvas/ScreenRoot/ScreenSplitArea/LowerSafe/ExchangDetailList/ScrollView/Viewport/Content/ExchangeDetailListCell(Clone)/Footer/RequiredResourceAmount",
    "WindowSystem/WindowRoot/ShopWindow(Clone)/LoginBonusShopScreen(Clone)/ScreenCanvas/ScreenRoot/ScreenSplitArea/Fullscreen/Upper/Header/PriceFrame/Price",
    "WindowSystem/WindowRoot/ShopWindow(Clone)/MarketScreen(Clone)/ScreenCanvas/ScreenRoot/ScreenSplitArea/Fullscreen/Upper/Header/MarketItemThumbnail/FrontRoot/PurchasePrice/ConsumptionIcon/Price",
    "WindowSystem/WindowRoot/ShopWindow(Clone)/MarketScreen(Clone)/ScreenCanvas/ScreenRoot/ScreenSplitArea/Fullscreen/Upper/Header/MarketItemThumbnail/FrontRoot/PurchasePrice/StoneAndGold/Price",
};

inline bool isShopPrice(std::string_view path) {
    for (const auto price : shopPricePaths) if (path == price) return true;
    return false;
}

inline bool isActivitySkipTime(std::string_view path) {
    constexpr std::string_view root = "ActGameShortenItemActionSheet(Clone)/Canvas/Root/UIContentArea/SheetContainer/ContentContainer/ContentMask/ContentViewport/Content/EffectAmount/AmountRoot/";
    const auto start = path.find(root);
    if (start == std::string_view::npos || (start && path[start - 1] != '/')) return false;
    const auto leaf = path.substr(start + root.size());
    return leaf == "BeforeAmount" || leaf == "AfterAmount";
}

inline bool isOriginalFontCandidate(std::string_view objectName) {
    return objectName == "BigTitle" || objectName == "Rank" || objectName == "Stage" ||
        objectName == "TotalPtNum" || objectName == "Point" ||
        objectName == "BeforeAmount" || objectName == "AfterAmount" ||
        objectName == "ActivityLevel" || objectName == "ManagerLevel" || objectName == "Level" ||
        objectName == "GaugeValue" || objectName == "ManagerPointUp" ||
        objectName == "FinishText" || objectName == "ThanksText";
}

inline bool needsPath(std::string_view objectName) {
    return isOriginalFontCandidate(objectName) || objectName == "Price" ||
        objectName == "ProductPrice" || objectName == "RequiredResourceAmount" || objectName == "NameText" ||
        objectName == "LevelText" || objectName == "Title" || objectName == "360" || objectName == "LIVE";
}

inline bool isFriendIdentity(std::string_view path) {
    const bool friendList = path.find("ShowcaseGalleryFriendSearchSheet(Clone)/") != std::string_view::npos ||
        path.find("/FriendPage/") != std::string_view::npos;
    if (!friendList) return false;
    constexpr std::string_view base = "/FriendCardListCell(Clone)/Button/ProfileCard/ProfileDataGroup/";
    const auto card = path.rfind(base);
    if (card == std::string_view::npos) return false;
    const auto leaf = path.substr(card + base.size());
    return leaf == "Name/NameText" || leaf == "Level/Title" || leaf == "Level/LevelText";
}

inline bool keepsOriginalDesign(std::string_view path) {
    if (isShopPrice(path) || isActivitySkipTime(path) || activity_result_layout::nativeNumber(path) ||
        activity_result_layout::finishAnimatedText(path)) return true;
    for (const auto original : originalFontPaths) if (path == original) return true;
    return false;
}

// Full-width ASCII punctuation/digits have the same routing as half-width.
// Do not use locale-sensitive iswpunct: Android's locale must not change which
// font a label receives. Keep this limited to punctuation used with numbers;
// letters, ideographs, markup and private-use sprite codes remain replacement.
inline bool isNumericSymbol(uint32_t c) {
    if (c >= 0xff01 && c <= 0xff5e) c -= 0xfee0;
    if ((c >= 0x21 && c <= 0x2f) || (c >= 0x3a && c <= 0x40) ||
        (c >= 0x5b && c <= 0x60) || (c >= 0x7b && c <= 0x7e)) return true;
    return (c >= 0xa2 && c <= 0xa5) || c == 0xb0 || c == 0xb1 || c == 0xb7 ||
        c == 0xd7 || c == 0xf7 || (c >= 0x2010 && c <= 0x2015) ||
        (c >= 0x2018 && c <= 0x201f) || c == 0x2026 ||
        (c >= 0x2030 && c <= 0x2033) || c == 0x2044 ||
        (c >= 0x20a0 && c <= 0x20c0) || c == 0x2212 || c == 0x2236 ||
        c == 0x2264 || c == 0x2265 || c == 0x3001 || c == 0x3002 ||
        (c >= 0x3008 && c <= 0x3011) || c == 0x3014 || c == 0x3015 || c == 0x301c ||
        c == 0xffe0 || c == 0xffe1 || c == 0xffe5 || c == 0xffe6;
}

// Signed amounts, percentages, clocks and fractions use the original font as
// one label. This classifies characters, not whether a number is well formed.
// Require a digit so a dialogue containing only "……" keeps its story font.
template <typename ReadChar>
inline bool isNumericSymbolText(size_t length, ReadChar read) {
    if (!length || length > 128) return false;
    bool digit = false;
    for (size_t i = 0; i < length; ++i) {
        const uint32_t c = static_cast<uint32_t>(read(i));
        if ((c >= '0' && c <= '9') || (c >= 0xff10 && c <= 0xff19)) { digit = true; continue; }
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == 0xa0 ||
            (c >= 0x2000 && c <= 0x200a) || c == 0x202f || c == 0x3000) continue;
        if (!isNumericSymbol(c)) return false;
    }
    return digit;
}

template <typename UnicodeChar>
inline bool isNumericSymbolBuffer(const UnicodeChar* text, size_t capacity) {
    if (!text || !capacity || capacity > 65536) return false;
    size_t length = 0;
    while (length < capacity && length <= 128 && text[length].unicode != 0) ++length;
    if (length >= capacity || length > 128) return false;
    return isNumericSymbolText(length, [&](size_t i) { return text[i].unicode; });
}

inline bool usesReplacement(std::string_view path, bool numericSymbols = false) {
    if (isShopPrice(path) || isActivitySkipTime(path) || activity_result_layout::nativeNumber(path) ||
        activity_result_layout::finishAnimatedText(path)) return false;
    if (isFriendIdentity(path) || !panorama_font_labels::labelLeaf(path).empty()) return true;
    for (const auto original : originalFontPaths) if (path == original) return false;
    return !numericSymbols;
}

enum class PreRenderRoute { Defer, Replacement, Original };
inline PreRenderRoute preRenderRoute(std::string_view path, bool glyphKnown, bool glyphNumeric,
                                    bool getterKnown, bool getterNumeric) {
    if (keepsOriginalDesign(path)) return PreRenderRoute::Original;
    if (isFriendIdentity(path) || !panorama_font_labels::labelLeaf(path).empty()) return PreRenderRoute::Replacement;
    // A formatted label's final buffer takes precedence over its string getter.
    if (glyphKnown) return glyphNumeric ? PreRenderRoute::Original : PreRenderRoute::Replacement;
    if (!getterKnown || getterNumeric) return PreRenderRoute::Defer;
    return PreRenderRoute::Replacement;
}
} // namespace ui_font_policy
