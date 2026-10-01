#pragma once
#include <cstddef>
#include <string_view>

namespace ui_font_policy {
// Default UI font is RHR. Retain observed decorative titles, compact rank
// badges and purchase-price labels whose original metrics are part of the layout.
inline constexpr std::string_view originalFontPaths[] = {
    "WindowSystem/WindowRoot/ShowcaseWindow(Clone)/ShowcaseTopScreen(Clone)/ScreenCanvas/ScreenRoot/ScreenWithHeroImage/FullScreen/Upper/HeaderImageContainer/TItleRoot/BigTitle",
    "WindowSystem/WindowRoot/StoryWindow(Clone)/CardStoryScreen(Clone)/ScreenCanvas/ScreenRoot/ScreenWithHeroImage/FullScreen/Upper/HeaderImageContainer/TItleRoot/BigTitle",
    "WindowSystem/WindowRoot/LiveWindow(Clone)/LiveTopScreen(Clone)/ScreenCanvas/ScreenRoot/ScreenWithHeroImage/FullScreen/Upper/HeaderImageContainer/TItleRoot/BigTitle",
    "WindowSystem/WindowRoot/HomeWindow(Clone)/HomeTopScreen(Clone)/ScreenCanvas/ScreenRoot/HomeCompanyButton/Button/Rank",
    "WindowSystem/WindowRoot/HomeWindow(Clone)/HomeTopScreen(Clone)/ScreenCanvas/ScreenRoot/HomeVenusRankButton/Button/Rank",
    "WindowSystem/WindowRoot/LiveWindow(Clone)/LiveTopScreen(Clone)/ScreenCanvas/ScreenRoot/ScreenWithHeroImage/FullScreen/Upper/HeaderFrontContainer/HomeVenusRankButton/Button/Rank",
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

inline bool isOriginalFontCandidate(std::string_view objectName) {
    return objectName == "BigTitle" || objectName == "Rank";
}

inline bool needsPath(std::string_view objectName) {
    return isOriginalFontCandidate(objectName) || objectName == "Price" ||
        objectName == "ProductPrice" || objectName == "RequiredResourceAmount" || objectName == "NameText" ||
        objectName == "LevelText" || objectName == "Title";
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
    if (isShopPrice(path)) return true;
    for (const auto original : originalFontPaths) if (path == original) return true;
    return false;
}

// Numeric-only counts retain the game's numerals and unobtrusive thousands
// separator. Units, fractions, times and rich-text markup are not counts.
template <typename ReadChar>
inline bool isNumericCount(size_t length, ReadChar read) {
    if (!length || length > 128) return false;
    size_t begin = 0, end = length;
    const auto whitespace = [](auto c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (begin < end && whitespace(read(begin))) ++begin;
    while (end > begin && whitespace(read(end - 1))) --end;
    if (begin == end) return false;
    size_t digits = 0;
    bool grouped = false;
    for (size_t i = begin; i < end; ++i) {
        const auto c = read(i);
        if (c >= '0' && c <= '9') { ++digits; continue; }
        if (c != ',' || !digits || (!grouped && digits > 3) || (grouped && digits != 3)) return false;
        grouped = true;
        digits = 0;
    }
    return grouped ? digits == 3 : digits > 0;
}

template <typename UnicodeChar>
inline bool isNumericBuffer(const UnicodeChar* text, size_t capacity) {
    if (!text || !capacity || capacity > 65536) return false;
    size_t length = 0;
    while (length < capacity && length <= 128 && text[length].unicode != 0) ++length;
    if (length >= capacity || length > 128) return false;
    return isNumericCount(length, [&](size_t i) { return text[i].unicode; });
}

inline bool usesReplacement(std::string_view path, bool numericCount = false) {
    if (isShopPrice(path)) return false;
    if (isFriendIdentity(path)) return true;
    for (const auto original : originalFontPaths) if (path == original) return false;
    return !numericCount;
}

enum class PreRenderRoute { Defer, Replacement, Original };
inline PreRenderRoute preRenderRoute(std::string_view path, bool glyphKnown, bool glyphNumeric,
                                    bool getterKnown, bool getterNumeric) {
    if (keepsOriginalDesign(path)) return PreRenderRoute::Original;
    if (isFriendIdentity(path)) return PreRenderRoute::Replacement;
    // A formatted label's final buffer takes precedence over its string getter.
    if (glyphKnown) return glyphNumeric ? PreRenderRoute::Original : PreRenderRoute::Replacement;
    if (!getterKnown || getterNumeric) return PreRenderRoute::Defer;
    return PreRenderRoute::Replacement;
}
} // namespace ui_font_policy
