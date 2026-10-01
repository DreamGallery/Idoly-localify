#include "../app/src/main/cpp/mixed_font_router.h"
#include <cassert>
#include <string>
#include <vector>
int main() {
    const auto route = [](const std::u16string& text) { return mixed_font_router::hasCjk(text.data(), text.size()); };
    assert(!route(u"360°LIVE Rank 123 !?"));
    assert(route(u"等级 Rank 123"));
    // A 3D TMP setter replaces its old Latin value with a CJK countdown.
    // Classify only after the assignment; UGUI pre-render is not guaranteed.
    const std::u16string previous = u"12:00";
    const std::u16string assigned = u"12:00 开始";
    assert(!route(previous) && route(assigned));
    assert(route(u"12:00 開始"));
    // CJK -> non-CJK must restore the original font after setter completion.
    assert(!route(u"MAX"));
    // One label's formatted buffer cycles CJK -> Latin -> CJK. The getter may
    // still expose the old Latin value; the buffer alone decides the font.
    using mixed_font_router::UnicodeChar;
    const UnicodeChar cjkBuffer[] = {{'1',0,1},{'2',1,1},{':',2,1},{'0',3,1},{'0',4,1},{0x5f00,5,1},{0x59cb,6,1},{0,7,0}};
    const UnicodeChar latinBuffer[] = {{'M',0,1},{'A',1,1},{'X',2,1},{0,3,0},{0x5f00,4,1}};
    assert(mixed_font_router::hasCjkBuffer(cjkBuffer, 8));
    assert(!mixed_font_router::hasCjkBuffer(latinBuffer, 5));
    assert(mixed_font_router::hasCjkBuffer(cjkBuffer, 8));
    const UnicodeChar supplementary[] = {{0x20000,0,2},{0,2,0}};
    assert(mixed_font_router::hasCjkBuffer(supplementary, 2));
    assert(!route(u"，。！？—♥😀"));
    assert(route(u"あいうカナﾞ"));
    assert(route(u"𠀀"));
    assert(route(u"𰀀"));
    assert(!route(u"😀 123"));
    assert(!route(std::u16string(1, 0xd800)));
    assert(route(u"々〇〻"));
    assert(!route(u"<color=red>ABC</color>"));
    // Raw rich-text attributes are scanned too; documented first-version limit.
    assert(route(u"<link=中文>ABC</link>"));
    assert(!mixed_font_router::isCjk(0x30fb));
    assert(!mixed_font_router::isCjk(0x33480));
    const std::string shopHeader = "WindowSystem/WindowRoot/ShowcaseWindow(Clone)/NormalShopScreen(Clone)/ScreenCanvas/ScreenRoot/ScreenSplitArea/Fullscreen/Upper/Header/";
    const auto bgmRoute = [](const std::u32string& value, const std::string& path) {
        std::vector<UnicodeChar> buffer;
        for (auto c : value) buffer.push_back({static_cast<int32_t>(c), 0, 1});
        buffer.push_back({0, 0, 0});
        return mixed_font_router::hasCjkBuffer(buffer.data(), buffer.size()) ||
            (mixed_font_router::isKnownShowcaseBgmBuffer(buffer.data(), buffer.size()) &&
             mixed_font_router::isShowcaseBgmHeader(path));
    };
    assert(bgmRoute(U"MELODIES", shopHeader + "Name"));
    assert(bgmRoute(U"Happy Stars Forever!", shopHeader + "Description"));
    assert(bgmRoute(U"lumière", shopHeader + "Name"));
    assert(bgmRoute(U"Colorful×Parallel", shopHeader + "Name"));
    // The same pooled header returns to the original font for non-BGM items.
    assert(!bgmRoute(U"123456", shopHeader + "Name"));
    assert(!bgmRoute(U"UNKNOWN NEW SONG", shopHeader + "Name"));
    assert(!bgmRoute(U"MELODIES badge", shopHeader + "Description"));
    assert(!bgmRoute(U"MELODIES", shopHeader + "LeftCount"));
    assert(!bgmRoute(U"MELODIES", shopHeader + "Name/Other"));
    assert(!bgmRoute(U"MELODIES", "Other/" + shopHeader + "Name"));
    assert(!bgmRoute(U"MELODIES", "WindowSystem/WindowRoot/MusicWindow/Name"));
    assert(bgmRoute(U"中文曲名", "WindowSystem/WindowRoot/MusicWindow/Name"));
    assert(!bgmRoute(U"360°LIVE Rank 123 !?", "WindowSystem/WindowRoot/MusicWindow/Name"));
    assert(bgmRoute(U"MELODIES", shopHeader + "Name"));
}
