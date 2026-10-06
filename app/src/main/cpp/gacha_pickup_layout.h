#pragma once
#include <initializer_list>
#include <string_view>

namespace gacha_pickup_layout {
enum class Label { None, Instruction, DrawPrompt };

inline Label classify(std::string_view path, std::string_view text) {
    constexpr std::string_view root = "GachaWindow(Clone)/NormalGachaScreen(Clone)/NormalGachaSheet(Clone)/Canvas/Root/UIContentArea/SheetContainer/ContentContainer/ContentMask/ContentViewport/Content/";
    const auto start = path.find(root);
    if (start == std::string_view::npos || (start && path[start - 1] != '/')) return Label::None;
    const auto local = path.substr(start + root.size());
    if (local == "PickupInfo/Description" && text == "请选择作为精选对象的偶像。")
        return Label::Instruction;
    // Recognize the previously shipped line breaks too: code and dictionary
    // updates can arrive separately. Never alter other disabled-button reasons.
    if (text != "请完成精选。" && text != "请完成\n精选。" && text != "请完成\n精选\n。")
        return Label::None;
    for (const auto prefix : {std::string_view("DrawButtons/StoneButton"),
                              std::string_view("OtherButtons/OtherButton")}) {
        if (local.size() == prefix.size() + 13 && local.substr(0, prefix.size()) == prefix &&
            local[prefix.size()] >= '1' && local[prefix.size()] <= '3' &&
            local.substr(prefix.size() + 1) == "/description") return Label::DrawPrompt;
    }
    return Label::None;
}
} // namespace gacha_pickup_layout
