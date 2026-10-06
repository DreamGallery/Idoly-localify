#pragma once
#include <string_view>

namespace premium_gacha_labels {
inline constexpr std::string_view purchaseParticlePath =
    "WindowSystem/WindowRoot/HomeWindow(Clone)/GachaWindow(Clone)/"
    "PremiumGachaScreen(Clone)/ScreenCanvas/ScreenRoot/ButtonRoot/"
    "ShopButton/Text/Layout/Sub";

inline bool hidePurchaseParticle(std::string_view text, std::string_view path,
                                 bool purchaseLabelsTranslated) {
    // This is the fixed ShopButton, never the changing ticket/draw button.
    return purchaseLabelsTranslated && text == "を" && path == purchaseParticlePath;
}
}
