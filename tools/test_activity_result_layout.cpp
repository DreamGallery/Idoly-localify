#include "../app/src/main/cpp/activity_result_layout.h"
#include <cassert>
#include <limits>
#include <string>
int main() {
    using namespace activity_result_layout;
    for (const auto* screen : {"PromotionResultScreen", "FanEventResultScreen", "RefreshResultScreen"}) {
        const auto name = std::string("WindowSystem/WindowRoot/ActivityWindow(Clone)/") + screen +
            "(Clone)/ScreenCanvas/ScreenRoot/Container/Information/ActivityResultActivityLevel/TextLayoutGroup/ActivityName";
        assert(activityName(name));
        assert(preserveTextSubmeshLayout(name,0,true,false));
        assert(preserveTextSubmeshLayout(name,1,true,false));
        assert(!preserveTextSubmeshLayout(name,2,true,false));
        assert(!preserveTextSubmeshLayout(name,0,false,false));
        assert(!preserveTextSubmeshLayout(name,0,true,true));
        assert(!preserveTextSubmeshLayout(name+"/Other",0,true,false));
        assert(!activityName(name + "/Other"));
        assert(!activityName("Other" + name.substr(name.find(screen))));
    }
    assert(activityName("PromotionScreen(Clone)/ScreenCanvas/ScreenRoot/ReceieveResult/Dialog/ActivityLevel/ActivityInfo/Name"));
    assert(!activityName("Other/ActivityInfo/Name"));
    // The receive popup uses left/middle and final results left/top. A centered
    // child shifts their glyphs by W/2 and (for results) -H/2 respectively.
    assert(needsPivotSync(0,.5f,.5f,.5f));
    assert(needsPivotSync(0,1,.5f,.5f));
    assert(!needsPivotSync(0,.5f,0,.5f));
    assert(!needsPivotSync(0,1,0,1));
    assert(!needsPivotSync(.5f,.5f,.5f,.5f));
    assert(!needsPivotSync(0,1,std::numeric_limits<float>::quiet_NaN(),.5f));
    assert(!needsPivotSync(0,1,2,.5f));
}
