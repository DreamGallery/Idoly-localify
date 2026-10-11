#pragma once
#include <string_view>

namespace love_font_policy {
inline bool usesRoundedReplacement(std::string_view originalName) {
    return originalName == "FOT-UDMarugo_SmallPr6-B SDF" ||
        originalName == "FOT-UDMarugo_SmallPr6-B Dynamic";
}
}
