#pragma once
#include <cstddef>
#include <cstdint>
#include "showcase_bgm_titles.h"
namespace mixed_font_router {
// Whole-label routing: any CJK codepoint selects the full replacement font.
constexpr bool isCjk(uint32_t c) {
    return c == 0x3005 || c == 0x3007 || c == 0x303b ||
        (c >= 0x3041 && c <= 0x3096) || (c >= 0x3099 && c <= 0x309f) ||
        (c >= 0x30a1 && c <= 0x30fa) || (c >= 0x30fc && c <= 0x30ff) ||
        (c >= 0x31f0 && c <= 0x31ff) || (c >= 0x3400 && c <= 0x4dbf) ||
        (c >= 0x4e00 && c <= 0x9fff) || (c >= 0xf900 && c <= 0xfaff) ||
        (c >= 0xff66 && c <= 0xff9f) || (c >= 0x1aff0 && c <= 0x1afff) ||
        (c >= 0x1b000 && c <= 0x1b16f) || (c >= 0x20000 && c <= 0x2fa1f) ||
        (c >= 0x30000 && c <= 0x3347f);
}
// Confirmed in game metadata: three System.Int32 fields in this order.
struct UnicodeChar { int32_t unicode; int32_t stringIndex; int32_t length; };
static_assert(sizeof(UnicodeChar) == 12, "TMP UnicodeChar layout");
// The shared NormalShopScreen also sells non-music items. Require both the
// captured Showcase header path and a complete known BGM title; never force
// fonts from song text alone, nor from this shared screen name alone.
inline bool isShowcaseBgmHeader(std::string_view path) {
    constexpr std::string_view root = "WindowSystem/WindowRoot/ShowcaseWindow(Clone)/NormalShopScreen(Clone)/ScreenCanvas/ScreenRoot/ScreenSplitArea/Fullscreen/Upper/Header/";
    if (path.substr(0, root.size()) != root) return false;
    const auto leaf = path.substr(root.size());
    return leaf == "Name" || leaf == "Description";
}
inline bool isKnownShowcaseBgmBuffer(const UnicodeChar* text, size_t capacity) {
    char32_t title[128];
    size_t length = 0;
    while (length < capacity && text[length].unicode != 0) {
        if (length == 128 || text[length].unicode <= 0 || text[length].unicode > 0x10ffff) return false;
        title[length] = static_cast<char32_t>(text[length].unicode);
        ++length;
    }
    const std::u32string_view value(title, length);
    for (const auto candidate : showcaseBgmTitles) if (candidate == value) return true;
    return false;
}
inline bool hasCjkBuffer(const UnicodeChar* text, size_t capacity) {
    for (size_t i = 0; i < capacity && text[i].unicode != 0; ++i)
        if (isCjk(static_cast<uint32_t>(text[i].unicode))) return true;
    return false;
}
inline bool hasCjk(const char16_t* text, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        uint32_t c = text[i];
        if (c >= 0xd800 && c <= 0xdbff && i + 1 < length &&
            text[i + 1] >= 0xdc00 && text[i + 1] <= 0xdfff)
            c = 0x10000 + ((c - 0xd800) << 10) + (text[++i] - 0xdc00);
        if (isCjk(c)) return true;
    }
    return false;
}
}
