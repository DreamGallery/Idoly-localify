#pragma once
#include <cstdint>
#include <string_view>

namespace story_punctuation {
inline bool isPunctuation(uint32_t c) {
    // Spaces, digits, Latin words, kana and ideographs retain their normal route.
    return (c >= 0x21 && c <= 0x2f) || (c >= 0x3a && c <= 0x40) ||
        (c >= 0x5b && c <= 0x60) || (c >= 0x7b && c <= 0x7e) ||
        (c >= 0x2010 && c <= 0x2027) || (c >= 0x2030 && c <= 0x2043) ||
        c == 0x3001 || c == 0x3002 || (c >= 0x3008 && c <= 0x3011) ||
        (c >= 0x3014 && c <= 0x301f) || c == 0x3030 || c == 0x30fb ||
        (c >= 0xff01 && c <= 0xff0f) || (c >= 0xff1a && c <= 0xff20) ||
        (c >= 0xff3b && c <= 0xff40) || (c >= 0xff5b && c <= 0xff65);
}
inline const char* profile(std::string_view path) {
    const auto ends = [path](std::string_view suffix) {
        return path.size() >= suffix.size() && path.substr(path.size() - suffix.size()) == suffix;
    };
    if (ends("/Message Container/Message Label")) return "ordinary_story_subtitle";
    if (ends("/Love UI/Message Area/Message Container/Message Root/Message Text"))
        return "portrait_love_event_dialogue";
    if (ends("/Bottom/SerifWIndow/Serif")) return "home_talk_dialogue";
    return nullptr;
}
}
