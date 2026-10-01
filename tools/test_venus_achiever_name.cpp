#include "../app/src/main/cpp/venus_achiever_name_matcher.h"
#include <cassert>
int main() {
    const std::string path = "WindowSystem/WindowRoot/VenusWindow(Clone)/VenusAchieverScreen(Clone)/ScreenCanvas/ScreenRoot/VenusAchieverList/Scroll/Viewport/Content/VenusAchieverListCell(Clone)/Cell/FirstAchieverName";
    const auto translate = [&](const std::string& value, const std::string& target) {
        return VenusAchieverNameMatcher::translate(value, target, "最快到达：{0}");
    };
    assert(translate("最速到達：マネージャー名12文字", path) == "最快到达：マネージャー名12文字");
    assert(translate("最速到達: Test 玩家♥", path) == "最快到达：Test 玩家♥");
    assert(translate("最速到達：Test", path + "/Other").empty());
    assert(translate("最速到達：Test", "Other/" + path).empty());
    assert(translate("最速到達：", path).empty());
    assert(translate("最速到達:{0}", path).empty());
    assert(translate("最速到達: {0}", path).empty());
    assert(translate("最快到达：Test", path).empty());
    assert(translate("最速到達：line1\nline2", path).empty());
    assert(translate("最速到達：" + std::string(257, 'A'), path).empty());
    assert(VenusAchieverNameMatcher::translate("最速到達：Test", path, "{0} {0}").empty());
    assert(VenusAchieverNameMatcher::translate("最速到達：Test", path, "最快到达").empty());
}
