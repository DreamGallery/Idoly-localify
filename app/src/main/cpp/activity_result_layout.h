#pragma once
#include <string_view>
#include <cmath>

namespace activity_result_layout {
// The in-progress receive popup is a separate prefab from the final results.
// Promotion's spelling is kept exactly as serialized by the game.
inline std::string_view receiveChild(std::string_view path) {
    constexpr std::string_view root = "PromotionScreen(Clone)/ScreenCanvas/ScreenRoot/ReceieveResult/Dialog/";
    const auto pos = path.find(root);
    if (pos == std::string_view::npos || (pos && path[pos - 1] != '/')) return {};
    return path.substr(pos + root.size());
}
// Verified original prefabs share these children. Match the screen boundary
// as well as the leaf so similarly named controls elsewhere keep their policy.
inline std::string_view informationChild(std::string_view path) {
    constexpr std::string_view screens[] = {
        "PromotionResultScreen(Clone)/", "FanEventResultScreen(Clone)/",
        "RefreshResultScreen(Clone)/",
    };
    constexpr std::string_view root = "ScreenCanvas/ScreenRoot/Container/Information/";
    for (auto screen : screens) {
        const auto pos = path.find(screen);
        if (pos == std::string_view::npos || (pos && path[pos - 1] != '/')) continue;
        const auto child = path.substr(pos + screen.size());
        if (child.substr(0, root.size()) == root) return child.substr(root.size());
    }
    return {};
}

inline bool nativeNumber(std::string_view path) {
    const auto popup = receiveChild(path);
    if (popup == "ActivityLevel/ActivityInfo/Level" || popup == "ManagerExp/Level" ||
        popup == "PromotionReceiveResultCharacter/Status/GaugeRoot/Level") return true;
    const auto child = informationChild(path);
    return child == "ActivityResultActivityLevel/TextLayoutGroup/ActivityLevel" ||
        child == "ActivityResultActivityLevel/Gauge/GaugeValue" ||
        child == "ActivityResultManagerExp/ManagerExpGauge/ManagerLevel" ||
        child == "ActivityResultManagerExp/ManagerExpGauge/Gauge/GaugeValue" ||
        child == "ActivityResultManagerExp/ManagerPointUp";
}

inline bool activityName(std::string_view path) {
    return informationChild(path) == "ActivityResultActivityLevel/TextLayoutGroup/ActivityName" ||
        receiveChild(path) == "ActivityLevel/ActivityInfo/Name";
}

inline bool preserveTextSubmeshLayout(std::string_view path,int axis,bool controlSelfSize,bool explicitChild) {
    return (axis == 0 || axis == 1) && controlSelfSize && !explicitChild && activityName(path);
}

inline bool needsPivotSync(float ownerX, float ownerY, float childX, float childY) {
    for (float v : {ownerX,ownerY,childX,childY})
        if (!std::isfinite(v) || v < 0 || v > 1) return false;
    return std::fabs(ownerX-childX) > .0001f || std::fabs(ownerY-childY) > .0001f;
}

inline bool aileLabel(std::string_view path) {
    return informationChild(path) == "ActivityResultRewards/Rewards/ActivityResultRewardView(Clone)/FadeResourceThumbnail/AIleNoti/Aile" ||
        receiveChild(path) == "RewardsContainer/ActivityResultRewardView(Clone)/FadeResourceThumbnail/AIleNoti/Aile";
}

inline bool finishAnimatedText(std::string_view path) {
    constexpr std::string_view screens[] = {
        "PromotionScreen(Clone)/", "FanEventScreen(Clone)/", "RefreshScreen(Clone)/",
    };
    constexpr std::string_view root = "ScreenCanvas/ScreenRoot/ActGameFinish/";
    for (auto screen : screens) {
        const auto pos = path.find(screen);
        if (pos == std::string_view::npos || (pos && path[pos - 1] != '/')) continue;
        const auto child = path.substr(pos + screen.size());
        if (child.substr(0, root.size()) != root) continue;
        const auto label = child.substr(root.size());
        return label == "FinishTextPositionRoot/FinishTextAnimationRoot/FinishText" ||
            label == "ThanksTextPositionRoot/ThanksTextAnimationRoot/ThanksText";
    }
    return false;
}
} // namespace activity_result_layout
