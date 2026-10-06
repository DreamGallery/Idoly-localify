#include "../app/src/main/cpp/fan_event_ranking_matcher.h"
#include <cassert>
#include <unordered_map>

int main() {
    const std::unordered_map<std::string, std::string> names = {
        {"サイン会", "签名会"}, {"撮影会", "摄影会"}, {"握手会", "握手会"}};
    const std::string path = "WindowSystem/WindowRoot/ActivityWindow(Clone)/" + std::string(fan_event_ranking::pathSuffix);
    auto translate = [&](std::string_view text, std::string_view target) {
        return fan_event_ranking::translate(text, target, names, "{0} 前{1}名 ({2})", "组合{0}");
    };
    assert(translate("サイン会 前100名 (组合030)", path) == "签名会 前100名 (组合030)");
    assert(translate("サイン会 前50名 (组合0123)", path) == "签名会 前50名 (组合0123)");
    assert(translate("撮影会 前100名 (组合030)", path) == "摄影会 前100名 (组合030)");
    assert(translate("握手会 前100名 (组合030)", path) == "握手会 前100名 (组合030)");
    assert(translate("签名会 前100名 (组合030)", path) == "签名会 前100名 (组合030)");
    assert(fan_event_ranking::mayMatch("サイン会 前100名 (组合030)", names));
    assert(fan_event_ranking::mayMatch("签名会 前100名 (组合030)", names));
    assert(!fan_event_ranking::mayMatch("player 前100名 (组合030)", names));
    for (const auto* invalid : {"サイン会 前１００名 (组合030)", "サイン会 前1000000名 (组合030)",
        "サイン会 前100名 (组合ユーザー)", "サイン会 前100名 (组合-1)", "サイン会 前100名 (组合030) extra",
        "サイン会 前100名 (组合030)\n", "サイン会 前名 (组合030)", "player 前100名 (组合030)"})
        assert(translate(invalid, path).empty());
    assert(translate("サイン会 前100名 (组合030)", "Other/Description").empty());
    assert(translate("サイン会 前100名 (组合030)", path + "/Child").empty());
    assert(translate("サイン会 前100名 (组合030)", "Other" + std::string(fan_event_ranking::pathSuffix)).empty());
    assert(fan_event_ranking::translate("サイン会 前50名 (小组007)", path, names,
        "{0} 前{1}名 ({2})", "小组{0}") == "签名会 前50名 (小组007)");
    for (const auto* broken : {"{0} 前{1}名 ({2}){2}", "{0} 前{1}名 ({user})", "{0} 前{2}名 ({1})"})
        assert(fan_event_ranking::translate("サイン会 前100名 (组合030)", path, names, broken, "组合{0}").empty());
}
