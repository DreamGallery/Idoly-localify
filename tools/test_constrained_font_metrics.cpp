#include "../app/src/main/cpp/constrained_font_metrics.h"
#include <cassert>
int main() {
    const std::string path = "WindowSystem/WindowRoot/ForumWindow(Clone)/ForumListScreen(Clone)/ScreenCanvas/ScreenRoot/ContentRoot/ForumThreadRoot/ForumThreadList/Scroll/Viewport/Content/ForumThreadListCell(Clone)/Button/Title";
    const FontVerticalMetrics rhr{90,1,104.4f,-25.92f}, original{34,1,29.92f,-4.08f};
    assert(forumTitleMetricsGuard(path,740,49,48,1,rhr,original));
    assert(forumTitleMetricsGuard(path,740,49,48,3,rhr,original));
    assert(!forumTitleMetricsGuard(path,740,49,48,0,rhr,original));
    assert(!forumTitleMetricsGuard(path,740,49,40,1,rhr,original));
    assert(!forumTitleMetricsGuard(path,740,80,48,1,rhr,original));
    assert(!forumTitleMetricsGuard("Other/Title",740,49,48,1,rhr,original));
    assert(!forumTitleMetricsGuard(path,740,49,48,1,rhr,rhr));
    assert(!forumTitleMetricsGuard(path,740,49,48,1,original,original));
    assert(fontLineHeight(48,original) <= 49);
}
