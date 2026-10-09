#include <cassert>
#include <string>
#include "../app/src/main/cpp/home_activity_name_matcher.h"
#include "../app/src/main/cpp/numeric_text_template_matcher.h"

int main() {
    const std::string prefix = "WindowSystem/WindowRoot/HomeWindow(Clone)/HomeTopScreen(Clone)/"
        "ScreenCanvas/ScreenRoot/ActivityPage/SlideRoot/Parent/ActivityCells/";
    HomeActivityNameMatcher matcher;
    assert(matcher.size() == 0 && !matcher.mayMatch("サイン会 Lv28"));
    assert(matcher.add("サイン会", "签名会"));
    const std::string durationPath =
        "WindowSystem/WindowRoot/ActivityWindow(Clone)/FanEventCharacterScreen(Clone)/"
        "ScreenCanvas/ScreenRoot/ActivityCharacterSelectCommonUI/Fullscreen/"
        "ActivityCharacterSelectCommonUpper/FadeRoot/RequiredTime/RequiredTimeTitle";
    assert(matcher.mayMatchDuration("サイン会所需时间参考", "{0}所需时间参考"));
    assert(matcher.translateDuration("サイン会所需时间参考", durationPath, "{0}所需时间参考") == "签名会所需时间参考");
    assert(matcher.translateDuration("预计サイン会用时", durationPath, "预计{0}用时") == "预计签名会用时");
    assert(matcher.translateDuration("サイン会所需时间参考", durationPath + "/Other", "{0}所需时间参考").empty());
    assert(matcher.translateDuration("玩家所需时间参考", durationPath, "{0}所需时间参考").empty());
    assert(matcher.translateDuration("签名会所需时间参考", durationPath, "{0}所需时间参考").empty());
    for (const auto pattern : {"{0}", "{0}{0}", "{1}{0}", "{0}多余}"})
        assert(!matcher.mayMatchDuration("サイン会所需时间参考", pattern));
    for (const std::string slot : {"FanEvent", "Promotion", "Refresh"}) {
        const auto path = prefix + slot + "/NameBase/Name";
        assert(matcher.translate("サイン会 Lv28", path) == "签名会 Lv28");
        assert(matcher.translate("サイン会 Lv048", path) == "签名会 Lv048");
        assert(matcher.mayMatch("签名会 Lv28"));
        assert(matcher.isRendered("签名会 Lv28", path));
        assert(matcher.translate("签名会 Lv28", path).empty());
        assert(!matcher.isRendered("サイン会 Lv28", path));
    }
    const auto path = prefix + "FanEvent/NameBase/Name";
    for (const std::string& wrong : {std::string(), "Other/" + path, path + "/Other", prefix + "Other/NameBase/Name",
            prefix + "FanEvent/Other/Name", std::string("WindowRoot/HomeTopScreen(Clone)/NameBase/Name")}) {
        assert(matcher.translate("サイン会 Lv28", wrong).empty());
        assert(!matcher.isRendered("签名会 Lv28", wrong));
    }
    for (const auto* text : {"サイン会 Lv", "サイン会 Lv２８", "サイン会 Lv-1", "サイン会 Lv1.5",
            "サイン会 Lv1000000", "サイン会 Lv28 ", "サイン会 Lv28\n", "サイン会 Lv{name}",
            "player Lv28", "サイン会 Lv28 extra", "サイン会　Lv28"}) {
        assert(!matcher.mayMatch(text));
        assert(matcher.translate(text, path).empty());
    }
    assert(!matcher.add("サイン会", "Other"));
    assert(!matcher.add("", "签名会"));
    assert(!matcher.add("player name", "名字"));
    assert(!matcher.add("<b>活动</b>", "活动"));
    assert(!matcher.add("活动", "{user}"));
    assert(!matcher.add(std::string(65, 'x'), "活动"));
    assert(!matcher.add("签名会", "第三名称")); // Would retranslate our own output.
    HomeActivityNameMatcher reversed;
    assert(reversed.add("签名会", "第三名称"));
    assert(!reversed.add("サイン会", "签名会"));
    HomeActivityNameMatcher bounded;
    for (int i = 0; i < 64; ++i) assert(bounded.add("source" + std::to_string(i), "target" + std::to_string(i)));
    assert(!bounded.add("another", "另外"));
    NumericTextTemplateMatcher global;
    assert(!global.add("出演 Lv{number}", "出演 Lv{number}")); // Global guard stays intact.
    assert(matcher.add("休暇", "休假"));
    assert(matcher.add("休息", "休息"));
    for (const std::string screen : {"FanEvent", "Promotion", "Refresh"}) {
        const auto footer = "WindowSystem/WindowRoot/ActivityWindow(Clone)/" + screen +
            "Screen(Clone)/ScreenCanvas/ScreenRoot/ActGameFooter/ActGameInfo/Level";
        assert(matcher.translate("休暇 Lv4", footer) == "休假 Lv4");
        assert(matcher.translate("休暇 Lv004", footer) == "休假 Lv004");
        assert(matcher.translate("休息 Lv2", footer) == "休息 Lv2");
        assert(matcher.isRendered("休假 Lv4", footer));
        assert(matcher.translate("休假 Lv4", footer).empty());
        assert(matcher.translate("休暇 Lv4", "Other/" + footer).empty());
        assert(matcher.translate("休暇 Lv4", footer + "/Child").empty());
        assert(matcher.translate("玩家 Lv4", footer).empty());
        assert(matcher.translate("休暇 Lv4\n", footer).empty());
    }
}
